#pragma once

#include "network_manager.h"
#include "logger.h"
#include "renderer.h"
#include "input.h"
#include "chunk.h"
#include "player.h"
#include "asset_manager.h"
#include "player_system.h"
#include "day_night.h"

#include <chrono>
#include <random>
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <glm/gtc/matrix_transform.hpp>

#ifdef _WIN32
#include <conio.h> // Required for _kbhit() and _getch()
#endif

class Player;

class Game
{
private:
	bool isRunning = false;
	bool isReadyToDraw = false;
	bool isHost = false;
	bool isMultiplayerGame = false;

	Entity localPlayerEntity = INVALID_ENTITY;

	GLFWwindow* window;
	Scene scene;
	AssetManager assetManager;
	NetworkManager netManager;
	Chunk chunk;
	VulkanRenderer renderer;
	Input input;

	void ShutdownCleanly();
	void ProcessNetworkPackets();

public:
	void Init();
	void Loop();
};