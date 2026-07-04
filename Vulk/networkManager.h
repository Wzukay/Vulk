#pragma once

#include <iostream>
#include <queue>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <map>

#ifdef _WIN32
#define _WIN32_WINNT 0x0601
#endif
#define ASIO_STANDALONE
#include <asio.hpp>

#include "logger.h"

struct GamePacket {
    int packetType; // 1 = Map Data, 2 = Actions, 3 = Catchup, 4 = DISCONNECT
    std::string data;
    asio::ip::udp::endpoint sender; // Useful to see who sent what in P2P
};

class NetworkManager
{
private:
    asio::ip::udp::endpoint lobbyServerEndpoint;
    asio::ip::udp::endpoint currentHostEndpoint;

    bool amIHost = false;
    bool hasRequestedMap = false;

    asio::io_context context;
    asio::ip::udp::socket socket;
    std::mutex socketMutex;

    std::thread workerThread;

    // A list of all verified active peer connections
    std::vector<asio::ip::udp::endpoint> connectedPeers;
    // A separate list for peers we are currently trying to punch to
    std::vector<asio::ip::udp::endpoint> pendingPeers;

    // Thread-safe locks and storage
    std::mutex peersMutex;
    std::queue<GamePacket> incomingQueue;
    std::mutex queueMutex;

    std::map<std::string, std::chrono::steady_clock::time_point> peerHeartbeats;

    std::vector<char> readBuffer;

public:
    NetworkManager() : socket(context, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0)), readBuffer(2048) {}
    ~NetworkManager() {}

    bool IsHost() const { return amIHost; }
    void SetHostEndpoint(const asio::ip::udp::endpoint& endpoint) { currentHostEndpoint = endpoint; }
    asio::ip::udp::endpoint GetHostEndpoint() const { return currentHostEndpoint; }

    bool EstablishP2P(const std::string& introServerIp, int introServerPort);

    void SendPacketTo(const std::string& msg, const asio::ip::udp::endpoint& targetEndpoint);
	void SendPacketToServer(const std::string& msg);
    void BroadcastPacket(const std::string& message);
    bool PopIncomingPacket(GamePacket& outPacket);

    void LeaveSession();
    void RemovePeer(const asio::ip::udp::endpoint& ep);

    bool IsPeerConnected(const asio::ip::udp::endpoint& ep);
    int GetConnectedPeersCount();
    int GetPendingPeersCount();
    std::vector<asio::ip::udp::endpoint> GetConnectedPeers();

    void PromoteToHost();

    int GetLocalPort();

private:
    void ListenForData();
    void MoveToConnected(const asio::ip::udp::endpoint& ep);
    void StartHostTimeoutMonitor();
	void StartClientHostMonitor();
};