#pragma once

#include "renderer.h"
#include "input.h"
#include "chunk.h"
#include "player.h"
#include "asset_manager.h"
#include "player_system.h"
#include "day_night.h"
#include "scene_manager.h"
#include "audio.h"

#include <chrono>
#include <thread>
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

	GLFWwindow* window;
	SceneManager sceneManager;
	VulkanRenderer renderer;
	Input input;

	void ShutdownCleanly();
	void ProcessNetworkPackets();

public:
	void Init();
	void Loop();
};