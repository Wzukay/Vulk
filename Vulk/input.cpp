#include "input.h"


CameraData cam = { glm::vec3(100.0f, 60.0f, 250.0f), glm::normalize(glm::vec3(0.0f, sin(glm::radians(-30.0f)), -cos(glm::radians(-30.0f)))), glm::vec3(0.0f, 1.0f, 0.0f) };

CameraData Input::ProcessInput(GLFWwindow* window) {
    // 1. Handle Mode Toggle (F1 Key)
    bool f1Pressed = (glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS);
    if (f1Pressed && !f1KeyPressedLastFrame) {
        g_CurrentMode = (g_CurrentMode == ControlMode::Player) ? ControlMode::NoClip : ControlMode::Player;
        std::cout << "[Control System] Switched mode to: "
            << (g_CurrentMode == ControlMode::Player ? "PLAYER" : "NO-CLIP") << "\n";
    }
    f1KeyPressedLastFrame = f1Pressed;

    // 2. Execute Movement Based on Mode
    if (g_CurrentMode == ControlMode::NoClip) {
        // --- Free Flying No-Clip Mode ---
        // Original camera-relative fly code
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cam.pos += cameraSpeed * cam.front;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cam.pos -= cameraSpeed * cam.front;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cam.pos += glm::normalize(glm::cross(cam.up, cam.front)) * cameraSpeed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cam.pos -= glm::normalize(glm::cross(cam.up, cam.front)) * cameraSpeed;
    }
    else{}

	return cam;
}

void Input::ProcessMouse(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }

    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos; // Reversed Y
    lastX = (float)xpos;
    lastY = (float)ypos;

    float sensitivity = 0.1f;
    yaw += xoffset * sensitivity;
    pitch = std::clamp(pitch + (yoffset * sensitivity), -89.0f, 89.0f);

    // Update cam.front using spherical coordinates
    glm::vec3 direction;
    direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction.y = sin(glm::radians(pitch));                         
    direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cam.front = glm::normalize(direction);
}
