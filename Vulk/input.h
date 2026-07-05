#pragma once

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

struct CameraData {
	glm::vec3 pos;
	glm::vec3 front;
	glm::vec3 up;
};

extern CameraData cam;

class Input
{
private:	
	float yaw = -90.0f;
	float pitch = 0.0f;
	float lastX = 400.0f, lastY = 300.0f;
	bool firstMouse = true;

public:	
	float cameraSpeed = 0.35f;
	void ProcessMouse(GLFWwindow* window, double xpos, double ypos);
	CameraData ProcessInput(GLFWwindow* window);
};

