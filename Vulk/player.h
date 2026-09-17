#pragma once

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

#include "input.h"
#include "ecs.h"
#include "chunk.h"

class Player {
private:
    glm::vec3 position;
    glm::vec3 velocity;

    float movementSpeed = 40.0f;
    float gravity = -9.81f * 3.0f; // Adjusted for snappy gameplay feeling
    float jumpForce = 35.0f;
    float playerHeight = 6.0f;     // Camera offset from feet

    float maxSlopeAngle = 45.0f;
    float minSlopeDot;

    bool isGrounded = false;

public:
    Player(glm::vec3 startPos);

    static void Update(Registry& registry, Entity playerEntity, GLFWwindow* window, const glm::vec3& camFront, const glm::vec3& camUp, float deltaTime);
};