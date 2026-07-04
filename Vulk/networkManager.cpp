#include "networkManager.h"

bool NetworkManager::EstablishP2P(const std::string& introServerIp, int introServerPort) {
    asio::error_code ec;
    asio::ip::udp::endpoint serverEndpoint(asio::ip::make_address(introServerIp, ec), introServerPort);

    if (ec) {
        std::cout << "[Network] Invalid introduction server IP.\n";
        return false;
    }

    lobbyServerEndpoint = serverEndpoint;
    hasRequestedMap = false;

    // 1. Tell introduction server we are ready
    std::string regMsg = "REGISTER";
    socket.send_to(asio::buffer(regMsg), serverEndpoint, 0, ec);
    std::cout << "[Network] Registered with introduction server. Waiting for peer pairing...\n";

    // 2. Synchronously wait for the server to pass back the peer's info
    auto senderEndpoint = std::make_shared<asio::ip::udp::endpoint>();
    socket.async_receive_from(asio::buffer(readBuffer), *senderEndpoint,
        [this, senderEndpoint](asio::error_code ec, size_t len) {
            if (ec) {
                std::cout << "[Network] Async peer profile receive failed: " << ec.message() << "\n";
                return;
            }

            std::string manifest(readBuffer.data(), len);

            // Parse role assignment and address directory safely
            size_t pipePos = manifest.find('|');
            if (pipePos != std::string::npos) {
                std::string role = manifest.substr(0, pipePos);
                std::string peersData = manifest.substr(pipePos + 1);

                amIHost = (role == "HOST");

                if (peersData != "NONE" && !peersData.empty()) {
                    std::stringstream ss(peersData);
                    std::string item;
                    std::lock_guard<std::mutex> lock(peersMutex);

                    bool firstItem = true;
                    while (std::getline(ss, item, ',')) {
                        size_t colon = item.find(':');
                        if (colon != std::string::npos) {
                            std::string ip = item.substr(0, colon);
                            int port = std::stoi(item.substr(colon + 1));
                            asio::ip::udp::endpoint peerEp(asio::ip::make_address(ip), port);

                            pendingPeers.push_back(peerEp);

                            if (!amIHost && firstItem) {
                                currentHostEndpoint = peerEp;
                                firstItem = false;
                            }
                        }
                    }
                }
            }

            // Once the manifest is parsed, start standard game loop operations
            ListenForData();

            if (amIHost) {
                StartHostTimeoutMonitor();
            }
            else {
                StartClientHostMonitor();
            }

            // Start Punching Loop thread context
            std::thread punchThread([this]() {
                std::string punchMsg = "PUNCH";
                for (int i = 0; i < 25; ++i) {
                    std::vector<asio::ip::udp::endpoint> pendingCopy;
                    {
                        std::lock_guard<std::mutex> lock(peersMutex);
                        if (pendingPeers.empty() && !connectedPeers.empty()) break;
                        pendingCopy = pendingPeers;
                    }

                    if (!pendingCopy.empty()) {
                        std::lock_guard<std::mutex> sockLock(socketMutex);
                        asio::error_code ignore_ec;
                        for (const auto& p : pendingCopy) {
                            socket.send_to(asio::buffer(punchMsg), p, 0, ignore_ec);
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                });
            punchThread.detach();
        });
    workerThread = std::thread([this]() { context.run(); });

    std::thread pingSender([this]() {
        std::string pingMsg = "PING";
        while (socket.is_open()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            std::vector<asio::ip::udp::endpoint> peersCopy;
            {
                std::lock_guard<std::mutex> lock(peersMutex);
                peersCopy = connectedPeers;
            }

            if (!peersCopy.empty()) {
                std::lock_guard<std::mutex> sockLock(socketMutex); // Prevent collisions
                asio::error_code ignore_ec;
                for (const auto& peer : peersCopy) {
                    socket.send_to(asio::buffer(pingMsg), peer, 0, ignore_ec);
                }
            }
        }
        });
    pingSender.detach();

    return true;
}

void NetworkManager::StartHostTimeoutMonitor() {
    debugLog.AddLog("[Network] Authoritative Host supervisor engaged.");
    std::thread hostTimeoutMonitor([this]() {
        while (socket.is_open()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            // Cut thread runtime execution instantly if a promotion event swapped configuration
            if (!amIHost) break;

            auto now = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(peersMutex);

            for (auto it = connectedPeers.begin(); it != connectedPeers.end();) {
                std::string epKey = it->address().to_string() + ":" + std::to_string(it->port());

                if (peerHeartbeats.count(epKey)) {
                    long long secondsElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - peerHeartbeats[epKey]).count();

                    if (secondsElapsed > 8) {
                        debugLog.AddLog("[TIMEOUT] Peer " + epKey + " missed pings for " + std::to_string(secondsElapsed) + "s.");

                        // 1. Alert the central Lobby Server on port 8888 to drop this stale endpoint
                        if (lobbyServerEndpoint.port() != 0) {
                            std::string dropMsg = "REMOVE_STALE:" + epKey;
                            std::lock_guard<std::mutex> sockLock(socketMutex);
                            asio::error_code serverEc;
                            socket.send_to(asio::buffer(dropMsg), lobbyServerEndpoint, 0, serverEc);
                        }

                        // 2. Queue the local broadcast to tell remaining active clients
                        GamePacket proxyDrop;
                        proxyDrop.packetType = 5;
                        proxyDrop.data = epKey;
                        proxyDrop.sender = *it;
                        {
                            std::lock_guard<std::mutex> qLock(queueMutex);
                            incomingQueue.push(proxyDrop);
                        }

                        // Clean out tracking structures safely
                        peerHeartbeats.erase(epKey);
                        it = connectedPeers.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
                else {
                    peerHeartbeats[epKey] = now;
                    ++it;
                }
            }
        }
        });
    hostTimeoutMonitor.detach();
}

void NetworkManager::StartClientHostMonitor() {
    debugLog.AddLog("[Network] Client heartbeat supervisor engaged. Watching Host endpoint...");
    std::thread clientHostMonitor([this]() {
        while (socket.is_open()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            // If promoted mid-game, terminate client thread context safely
            if (amIHost) break;

            auto now = std::chrono::steady_clock::now();
            std::string hostKey = currentHostEndpoint.address().to_string() + ":" + std::to_string(currentHostEndpoint.port());

            bool hostDead = false;
            {
                std::lock_guard<std::mutex> lock(peersMutex);
                if (peerHeartbeats.count(hostKey)) {
                    long long secondsElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - peerHeartbeats[hostKey]).count();
                    if (secondsElapsed > 8) {
                        hostDead = true;
                    }
                }
            }

            if (hostDead) {
                debugLog.AddLog("[MIGRATION] Host " + hostKey + " desynced! Dispatching migration order...");
                GamePacket migrationPacket;
                migrationPacket.packetType = 7; // Migration Action
                {
                    std::lock_guard<std::mutex> qLock(queueMutex);
                    incomingQueue.push(migrationPacket);
                }
                break; // Break execution frame safely
            }
        }
        });
    clientHostMonitor.detach();
}

void NetworkManager::SendPacketTo(const std::string& msg, const asio::ip::udp::endpoint& targetEndpoint) {
    std::lock_guard<std::mutex> lock(socketMutex); // Lock the socket hardware
    asio::error_code ec;
    socket.send_to(asio::buffer(msg.data(), msg.size()), targetEndpoint, 0, ec);
}

void NetworkManager::SendPacketToServer(const std::string& msg) {
    std::lock_guard<std::mutex> lock(socketMutex); // Lock the socket hardware
    asio::error_code ec;
    socket.send_to(asio::buffer(msg.data(), msg.size()), lobbyServerEndpoint, 0, ec);
}



void NetworkManager::BroadcastPacket(const std::string& message) {
    std::lock_guard<std::mutex> lock(socketMutex); // Lock the socket hardware
    asio::error_code ec;
    for (const auto& peer : connectedPeers) {
        socket.send_to(asio::buffer(message.data(), message.size()), peer, 0, ec);
    }
}
bool NetworkManager::PopIncomingPacket(GamePacket& outPacket) {
    std::lock_guard<std::mutex> lock(queueMutex);
    if (incomingQueue.empty()) return false;
    outPacket = incomingQueue.front();
    incomingQueue.pop();
    return true;
}

void NetworkManager::LeaveSession() {
    std::lock_guard<std::mutex> lock(peersMutex);
    asio::error_code ec;
    std::string departureMsg = "BYE";

    // Notify all peers immediately so they don't wait for a timeout
    for (const auto& peer : connectedPeers) {
        socket.send_to(asio::buffer(departureMsg), peer, 0, ec);
    }

    if (lobbyServerEndpoint.port() != 0) {
        socket.send_to(asio::buffer(departureMsg), lobbyServerEndpoint, 0, ec);
    }

    // Shut down local network context safely
    context.stop();
    if (socket.is_open()) socket.close();
    if (workerThread.joinable()) workerThread.join();
}
void NetworkManager::RemovePeer(const asio::ip::udp::endpoint& ep) {
    std::lock_guard<std::mutex> lock(peersMutex);

    std::string epKey = ep.address().to_string() + ":" + std::to_string(ep.port());

    // Clean out their tracking timestamps completely
    if (peerHeartbeats.count(epKey)) {
        peerHeartbeats.erase(epKey);
    }

    // Remove from the active endpoint directory array
    for (auto it = connectedPeers.begin(); it != connectedPeers.end();) {
        if (*it == ep) {
            std::cout << "\n[Network] Dropped peer connection: " << epKey << "\n";
            it = connectedPeers.erase(it);
        }
        else {
            ++it;
        }
    }
}

int NetworkManager::GetConnectedPeersCount() {
    std::lock_guard<std::mutex> lock(peersMutex);
    return static_cast<int>(connectedPeers.size());
}
int NetworkManager::GetPendingPeersCount() {
	std::lock_guard<std::mutex> lock(peersMutex);
	return static_cast<int>(pendingPeers.size());
}
bool NetworkManager::IsPeerConnected(const asio::ip::udp::endpoint& ep) {
    std::lock_guard<std::mutex> lock(peersMutex);
    for (const auto& peer : connectedPeers) {
        if (peer == ep) {
            return true;
        }
    }
    return false;
}

void NetworkManager::ListenForData() {
    auto senderEp = std::make_shared<asio::ip::udp::endpoint>();
    socket.async_receive_from(asio::buffer(readBuffer.data(), readBuffer.size()), *senderEp,
        [this, senderEp](asio::error_code ec, size_t bytes_transferred) {

            if (ec == asio::error::connection_reset || ec == asio::error::connection_refused) {
                ListenForData();
                return;
            }

            if (ec) {
                debugLog.AddLog("[NET ERROR] Recv failed: " + ec.message());
                ListenForData();
                return;
            }

            if (bytes_transferred > 0) {
                std::string incomingMsg(readBuffer.data(), bytes_transferred);

                {
                    std::string epKey = senderEp->address().to_string() + ":" + std::to_string(senderEp->port());
                    std::lock_guard<std::mutex> lock(peersMutex);
                    peerHeartbeats[epKey] = std::chrono::steady_clock::now();
                }

                // Safely check if this is a mid-game peer handshake update from the STUN/Intro server
                if (incomingMsg.rfind("NEW_PEER|", 0) == 0 && senderEp->port() == 8888) {
                    std::string peerInfo = incomingMsg.substr(9);
                    size_t colon = peerInfo.find(':');
                    if (colon != std::string::npos) {
                        std::string ip = peerInfo.substr(0, colon);
                        int port = std::stoi(peerInfo.substr(colon + 1));

                        asio::ip::udp::endpoint newClientEp(asio::ip::make_address(ip), port);

                        {
                            std::lock_guard<std::mutex> lock(peersMutex);
                            pendingPeers.push_back(newClientEp);
                        }

                        if (amIHost) {
                            std::lock_guard<std::mutex> sockLock(socketMutex);
                            asio::error_code ignore_ec;
                            std::string punchMsg = "PUNCH";
                            socket.send_to(asio::buffer(punchMsg), newClientEp, 0, ignore_ec);

                            GamePacket catchupNotice;
                            catchupNotice.packetType = 3; // 3 = Catchup Notification
                            catchupNotice.data = "SEND_MAP_TO_NEW_PEER";
                            catchupNotice.sender = newClientEp;

                            std::lock_guard<std::mutex> lock(queueMutex);
                            incomingQueue.push(catchupNotice);
                        }
                    }
                }
                else if (incomingMsg == "PUNCH") {
                    MoveToConnected(*senderEp);
                    std::string ack = "PUNCH_ACK";

                    std::lock_guard<std::mutex> sockLock(socketMutex); // Secure ACK
                    socket.send_to(asio::buffer(ack), *senderEp);
                }
                else if (incomingMsg == "PUNCH_ACK") {
                    MoveToConnected(*senderEp);
                }
                else if (incomingMsg.rfind("FORCE_DROP:", 0) == 0) {
                    GamePacket packet;
                    packet.packetType = 6; // Enforcement order from Host
                    packet.data = incomingMsg.substr(11);
                    {
                        std::lock_guard<std::mutex> qLock(queueMutex);
                        incomingQueue.push(packet);
                    }
                }
                else if (incomingMsg == "BYE") {
                    if (*senderEp == currentHostEndpoint) {
                        debugLog.AddLog("[NET] Host initiated clean exit. Migrating room master...");
                        GamePacket packet;
                        packet.packetType = 7;
                        std::lock_guard<std::mutex> qLock(queueMutex);
                        incomingQueue.push(packet);
                    }
                    else {
                        debugLog.AddLog("[NET] Recv BYE from peer " + senderEp->address().to_string() + ":" + std::to_string(senderEp->port()));
                        GamePacket packet;
                        packet.packetType = 4;
                        packet.sender = *senderEp;
                        std::lock_guard<std::mutex> qLock(queueMutex);
                        incomingQueue.push(packet);
                    }
                }
                else if (incomingMsg == "PING") {
                    // Drop silently, heartbeat is already refreshed above
                }
                else {
                    // Standard Payload Packet Routing
                    GamePacket packet;
                    packet.packetType = 2; // Default game action packet type

                    if (incomingMsg.rfind("MAP_DATA:", 0) == 0) {
                        packet.packetType = 1;
                        packet.data = incomingMsg.substr(9);
                    }
                    else if (incomingMsg == "REQ_MAP") {
                        packet.packetType = 3; // Catchup/Map Request type
                        packet.data = incomingMsg;
                    }
                    else {
                        packet.data = incomingMsg;
                    }

                    packet.sender = *senderEp;

                    std::lock_guard<std::mutex> qLock(queueMutex);
                    incomingQueue.push(packet);
                }

                ListenForData();
            }
        });
}

void NetworkManager::MoveToConnected(const asio::ip::udp::endpoint& ep) {
    std::lock_guard<std::mutex> lock(peersMutex);

    // Clear out from pending
    for (auto it = pendingPeers.begin(); it != pendingPeers.end();) {
        if (*it == ep) it = pendingPeers.erase(it);
        else ++it;
    }

    // Add to active mesh list if it's unique
    bool alreadyConnected = false;
    for (const auto& c : connectedPeers) {
        if (c == ep) {
            alreadyConnected = true;
            break;
        }
    }

    if (!alreadyConnected) {
        connectedPeers.push_back(ep);
        std::cout << "\n[Mesh Success] Connected to peer: " << ep.address().to_string() << ":" << ep.port() << "\n";

        // CRITICAL FIX: If we are a CLIENT, the moment we successfully connect 
        // to a peer (the host), send them an explicit map request packet!
        if (!amIHost && !hasRequestedMap) {
            hasRequestedMap = true;

            std::string reqMsg = "REQ_MAP";
            std::lock_guard<std::mutex> sockLock(socketMutex); // Keep socket writing thread-safe
            asio::error_code ec;
            socket.send_to(asio::buffer(reqMsg), ep, 0, ec);
        }
    }
}

void NetworkManager::PromoteToHost() {
    amIHost = true;
    StartHostTimeoutMonitor();
}

std::vector<asio::ip::udp::endpoint> NetworkManager::GetConnectedPeers() {
    std::lock_guard<std::mutex> lock(peersMutex);
    return connectedPeers;
}

int NetworkManager::GetLocalPort() {
    return static_cast<int>(socket.local_endpoint().port());
}