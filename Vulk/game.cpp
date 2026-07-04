#include "game.h"

GameLogger debugLog;
bool isReadyToDraw = false;

void Game::Init()
{ 
    std::cout << "Starting Base Vulkan Setup...\n";
    renderer.Initialize(1280, 720, "Vulk");

    window = renderer.GetWindow();

    glfwSetWindowUserPointer(window, &input);

    // Define the static callback that calls the Input class method
    glfwSetCursorPosCallback(window, [](GLFWwindow* window, double xpos, double ypos) {
        Input* inputPtr = reinterpret_cast<Input*>(glfwGetWindowUserPointer(window));
        inputPtr->ProcessMouse(window, xpos, ypos);
        });

    // Hide cursor for FPS feel
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    std::cout << "Setup complete! Running base frame check window loop...\n";

    if (isMultiplayerGame) {
        bool connected = netManager.EstablishP2P("127.0.0.1", 8888);
        if (!connected) {
            std::cout << "[Error] Could not initialize network stack.\n";
            system("pause");
            return;
        }

        debugLog.AddLog("[Lobby] Contacting introduction server. Standing by...");
    }
    else {
        worldGenerator.GetRandomSeed();
        worldGenerator.InitializeWorld(renderer, 200, 200);
        isReadyToDraw = true;
    }

    isRunning = true;
}

void Game::Loop()
{
    auto lastUpdate = std::chrono::steady_clock::now();
    bool firstRun = true;

    if (!window) {
        debugLog.AddLog("[Error] Window is null! Check Init().");
        return;
    }

    while (!renderer.ShouldClose() && isRunning) {
        renderer.PollEvents();

        if(isMultiplayerGame)
            ProcessNetworkPackets();

        CameraData cam = input.ProcessInput(window);

        if (isReadyToDraw) {
            renderer.UpdateUniformBuffer({ cam.pos, cam.front, cam.up });
        }

        renderer.DrawFrame();
    }

    std::cout << "Closing and dropping pipelines...\n";
    renderer.Cleanup();
    ShutdownCleanly();
}

void Game::ShutdownCleanly() {
    if (isRunning && isMultiplayerGame) {
        isRunning = false;
        std::cout << "[Shutdown] Sending departure message to mesh...\n";
        netManager.LeaveSession();
    }
}

void Game::ProcessNetworkPackets() {
    isHost = netManager.IsHost();

    if (isHost && !isReadyToDraw) {
        // Double check if we are truly ready (meaning we have received our manifest response)
        if (netManager.GetConnectedPeersCount() == 0 && netManager.GetPendingPeersCount() == 0) {
            debugLog.AddLog("[Lobby] Async role resolved: You are HOST. Generating world...");

            worldGenerator.GetRandomSeed();
            worldGenerator.InitializeWorld(renderer, 200, 200);
            isReadyToDraw = true;
        }
    }

    GamePacket packet;
    bool stateChanged = false;
    while (netManager.PopIncomingPacket(packet)) {
        std::cout << "[Packet Trace] Received packet type: " << packet.packetType
            << " | Data payload: '" << packet.data << "'\n";

        stateChanged = true;
        std::string epKey = packet.sender.address().to_string() + ":" + std::to_string(packet.sender.port());
        if (!isHost && packet.packetType == 1) {
            std::cout << "[SUCCESS] RECEIVED SEED FROM HOST: " << packet.data << "\n";
            debugLog.AddLog("[Client] Late-joining world downloaded. Rendering...");

            if (packet.data.empty()) {
                std::cout << "[Network Test] ERROR: Packet data string was completely empty!\n";
                continue;
            }

            try {
                std::string seedStr = packet.data;
                if (seedStr.rfind("MAP_DATA:", 0) == 0) {
                    seedStr = seedStr.substr(9); // Strip "MAP_DATA:" prefix
                }

                std::cout << "[Network Test] Attempting to parse seed token: '" << seedStr << "'\n";
                int seed = std::stoi(seedStr);

                worldGenerator.SetSeed(seed);
                worldGenerator.InitializeWorld(renderer, 200, 200);
                isReadyToDraw = true;

                std::cout << "[Network Test] SUCCESS! World buffers built for client.\n";
            }
            catch (const std::exception& e) {
                std::cout << "[Network Test] FAILED TO PARSE SEED. Exception: " << e.what() << "\n";
            }
        }
        else if (isHost && packet.packetType == 3) {
            debugLog.AddLog("[Hot-Plug] Late arrival peer detected: " + epKey);

            int currentSeed = worldGenerator.GetSeed();
            if (currentSeed == 0) {
                debugLog.AddLog("[DEBUG] Seed was 0 on host: " + epKey);
                currentSeed = 123456;
            }

            netManager.SendPacketTo("MAP_DATA:" + std::to_string(currentSeed), packet.sender);
        }
        else if (packet.packetType == 4) {
            debugLog.AddLog("[GAME] Dropping peer cleanly: " + epKey);
            netManager.RemovePeer(packet.sender);
        }
        else if (isHost && packet.packetType == 5) {
            debugLog.AddLog("[Host Authority] Syncing drop of dead peer: " + packet.data);
            netManager.BroadcastPacket("FORCE_DROP:" + packet.data);
        }
        else if (!isHost && packet.packetType == 6) {
            debugLog.AddLog("[Host Order] Removing disconnected peer from mesh: " + packet.data);
            size_t colon = packet.data.find(':');
            if (colon != std::string::npos) {
                std::string ip = packet.data.substr(0, colon);
                int port = std::stoi(packet.data.substr(colon + 1));
                asio::ip::udp::endpoint deadPeer(asio::ip::make_address(ip), port);
                netManager.RemovePeer(deadPeer);
            }
        }
        else if (packet.packetType == 7) { // Host Migration
            netManager.RemovePeer(netManager.GetHostEndpoint());
            auto remainingPeers = netManager.GetConnectedPeers();
            int myPort = netManager.GetLocalPort();
            int lowestPort = myPort;
            asio::ip::udp::endpoint candidateHost;
            bool iAmLowest = true;
            for (const auto& peer : remainingPeers) {
                if (static_cast<int>(peer.port()) < lowestPort) {
                    lowestPort = peer.port();
                    candidateHost = peer;
                    iAmLowest = false;
                }
            }
            
            if (iAmLowest) {
                isHost = true;
                netManager.PromoteToHost();
                debugLog.AddLog("[MIGRATION] I am the new Host! Notifying matchmaker...");
                netManager.SendPacketToServer("NEW_HOST_TAKEOVER");
            }
            else {
                netManager.SetHostEndpoint(candidateHost);
                debugLog.AddLog("[MIGRATION] New Host designated at port: " + std::to_string(lowestPort));
            }
        }
    }

    // 3. Render frame loop independently of incoming data packets
    auto now = std::chrono::steady_clock::now();
    if (stateChanged) {
        std::cout << " LIVE DIAGNOSTIC LOG MATRIX:\n";
        auto currentLogs = debugLog.GetLogs();
    
        for (size_t i = 0; i < 8; ++i) {
            if (i < currentLogs.size()) {
                std::cout << " > " << currentLogs[i] << "\n";
            }
            else {
                std::cout << " > ---\n";
            }
        }
        std::cout << "========================================\n";
        std::cout << (isHost ? "[ROLE: HOST]\n" : "[ROLE: CLIENT]\n");
        std::cout << "========================================\n";
    }
}