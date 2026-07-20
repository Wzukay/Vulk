#pragma once

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include "input.h"

class Player {
public:
    glm::vec3 position;
    glm::vec3 velocity;

    float movementSpeed = 40.0f;
    float gravity = -9.81f * 3.0f; // Adjusted for snappy gameplay feeling
    float jumpForce = 35.0f;
    float playerHeight = 6.0f;     // Camera offset from feet

    float maxSlopeAngle = 45.0f;
    float minSlopeDot;

    bool isGrounded = false;

    Player(glm::vec3 startPos);

    void Update(float deltaTime, GLFWwindow* window, const glm::vec3& camFront, const glm::vec3& camUp);
    void HandleTerrainCollisions(float deltaTime);
};