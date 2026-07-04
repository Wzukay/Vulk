#pragma once

#include "networkManager.h"
#include "worldGen.h"
#include "logger.h"
#include "renderer.h"
#include "input.h"

#include <iostream>
#include <string>
#include <vector>
#include <mutex>

#ifdef _WIN32
#include <conio.h> // Required for _kbhit() and _getch()
#endif

class Game
{
private:
	bool isRunning = false;
	bool isHost = false;
	bool isMultiplayerGame = true;

	GLFWwindow* window;

	NetworkManager netManager;
	WorldGen worldGenerator;
	VulkanRenderer renderer;
	WorldGen worldGen;
	Input input;

	void ShutdownCleanly();
	void ProcessNetworkPackets();

public:
	void Init();
	void Loop();
};