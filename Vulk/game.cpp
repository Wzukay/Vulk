#include "game.h"

GameLogger debugLog;
static glm::vec3 lastCamPos;
extern ControlMode g_CurrentMode;

void Game::Init()
{
    std::cout << "Starting Base Vulkan Setup...\n";

    g_Settings.LoadFromFile();

    renderer.Initialize(g_Settings.windowWidth, g_Settings.windowHeight, "Vulk");
    window = renderer.GetWindow();

    chunk.Init(renderer);

    g_AssetManager.LoadTextureFromFile("assets/textures/dirt_albedo.dds");       // ID 0
    g_AssetManager.LoadTextureFromFile("assets/textures/grass_albedo.dds");      // ID 1
    g_AssetManager.LoadTextureFromFile("assets/textures/rock_albedo.dds");       // ID 2
    g_AssetManager.LoadTextureFromFile("assets/textures/butterfly_albedo.dds");   // ID 4

    //g_AssetManager.LoadNormalTextureFromFile("assets/textures/dirt_normal.dds");       // ID 0
    //g_AssetManager.LoadNormalTextureFromFile("assets/textures/grass_normal.dds");      // ID 1
    //g_AssetManager.LoadNormalTextureFromFile("assets/textures/rock_normal.dds");       // ID 2

    g_AssetManager.LoadMesh("assets/models/tree/tree_lod0.glb");
    g_AssetManager.LoadMesh("assets/models/tree/tree_lod1.glb");
    g_AssetManager.LoadMesh("assets/models/tree/tree_lod2.glb");
    g_AssetManager.LoadMesh("assets/models/tree/tree_lod3.glb");
    g_AssetManager.LoadMesh("assets/models/rock/rock.glb");

    glfwSetWindowUserPointer(window, &input);
    glfwSetCursorPosCallback(window, [](GLFWwindow* window, double xpos, double ypos) {
        Input* inputPtr = reinterpret_cast<Input*>(glfwGetWindowUserPointer(window));
        inputPtr->ProcessMouse(window, xpos, ypos);
        });
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
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> distrib(1, 10);
        int myRandomInt = distrib(gen);

        chunk.SetSeed(myRandomInt);

        CameraData initialCam = { glm::vec3(100.0f, 60.0f, 250.0f),
                                  glm::normalize(glm::vec3(0.0f, sin(glm::radians(-30.0f)), -cos(glm::radians(-30.0f)))),
                                  glm::vec3(0.0f, 1.0f, 0.0f) };
        renderer.UpdateUniformBuffer(initialCam);

        chunk.Update(initialCam.pos, scene, renderer);
        isReadyToDraw = true;
    }

    localPlayerEntity = scene.GetRegistry().CreateEntity();

    TransformComponent pTransform;
    pTransform.position = glm::vec3(100.0f, 60.0f, 250.0f);
    scene.GetRegistry().AddComponent<TransformComponent>(localPlayerEntity, pTransform);

    PhysicsComponent pPhysics;
    scene.GetRegistry().AddComponent<PhysicsComponent>(localPlayerEntity, pPhysics);

    PlayerComponent pPlayer;
    pPlayer.minSlopeDot = std::cos(glm::radians(pPlayer.maxSlopeAngle)); // Cache our dot product limit cleanly
    scene.GetRegistry().AddComponent<PlayerComponent>(localPlayerEntity, pPlayer);

    renderer.UpdateScene(scene);

    isRunning = true;
}

void Game::Loop()
{
    auto lastUpdate = std::chrono::steady_clock::now();
    double lastFrameTime = glfwGetTime();
    bool firstRun = true;

    if (!window) {
        debugLog.AddLog("[Error] Window is null! Check Init().");
        return;
    }

    DayNightManager dayNight;

    while (!renderer.ShouldClose() && isRunning) {
        // --- NEW: Record Frame Start Time ---
        auto frameStartTime = std::chrono::high_resolution_clock::now();

        double currentFrameTime = glfwGetTime();
        float deltaTime = std::min(static_cast<float>(currentFrameTime - lastFrameTime), 0.1f);
        lastFrameTime = currentFrameTime;

        renderer.PollEvents();

        if (isMultiplayerGame)
            ProcessNetworkPackets();

        CameraData cam = input.ProcessInput(window);

        if (isReadyToDraw) {
            extern ControlMode g_CurrentMode;
            auto& transform = scene.GetRegistry().GetComponent<TransformComponent>(localPlayerEntity);
            auto& physics = scene.GetRegistry().GetComponent<PhysicsComponent>(localPlayerEntity);
            auto& playerOpt = scene.GetRegistry().GetComponent<PlayerComponent>(localPlayerEntity);

            if (g_CurrentMode == ControlMode::Player) {
                // Update player positions using our decoupled kinematics system
                PlayerSystem::Update(scene.GetRegistry(), window, cam.front, cam.up, deltaTime);
                cam.pos = transform.position + glm::vec3(0.0f, playerOpt.playerHeight, 0.0f);
            }
            else {
                // Freecam mode: update coordinates independently of physics
                transform.position = cam.pos - glm::vec3(0.0f, playerOpt.playerHeight, 0.0f);
                physics.velocity = glm::vec3(0.0f);
                transform.isDirty = true;
            }

            renderer.UpdateUniformBuffer({ cam.pos, cam.front, cam.up });

            chunk.Update(cam.pos, scene, renderer);

            dayNight.Tick(deltaTime, scene);

            renderer.UpdateScene(scene);
        }

        renderer.DrawFrame();

        if (g_Settings.frameCap > 0 && !g_Settings.vsync) {
            double targetMs = 1000.0 / g_Settings.frameCap;

            while (true) {
                auto currentTime = std::chrono::high_resolution_clock::now();
                double elapsedMs = std::chrono::duration<double, std::milli>(currentTime - frameStartTime).count();

                if (elapsedMs >= targetMs) {
                    break;
                }

                if (targetMs - elapsedMs > 1.0) {
                    std::this_thread::yield();
                }
            }
        }
    }

    std::cout << "Closing and dropping pipelines...\n";

    g_Settings.SaveToFile();

    chunk.Shutdown();
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

            chunk.SetRandomSeed();
            chunk.Update(cam.pos, scene, renderer);
            renderer.UpdateScene(scene);
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

                chunk.SetSeed(seed);
                chunk.Update(cam.pos, scene, renderer);
                renderer.UpdateScene(scene);
                isReadyToDraw = true;

                std::cout << "[Network Test] SUCCESS! World buffers built for client.\n";
            }
            catch (const std::exception& e) {
                std::cout << "[Network Test] FAILED TO PARSE SEED. Exception: " << e.what() << "\n";
            }
        }
        else if (isHost && packet.packetType == 3) {
            debugLog.AddLog("[Hot-Plug] Late arrival peer detected: " + epKey);

            int currentSeed = Chunk::s_globalSeed;
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