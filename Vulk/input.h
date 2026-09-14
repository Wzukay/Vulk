#pragma once

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>

struct CameraData {
	glm::vec3 pos;
	glm::vec3 front;
	glm::vec3 up;
};

enum class ControlMode {
	Player,
	NoClip
};

inline ControlMode g_CurrentMode = ControlMode::NoClip;

static bool f1KeyPressedLastFrame = false;

extern CameraData cam;

class Input
{
private:	
	float yaw = -90.0f;
	float pitch = -30.0f;
	float lastX = 400.0f, lastY = 300.0f;
	bool firstMouse = true;

public:	
	float cameraSpeed = 5;
	void ProcessMouse(GLFWwindow* window, double xpos, double ypos);
	CameraData ProcessInput(GLFWwindow* window);
};

