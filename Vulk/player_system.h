#pragma once

#include "ecs.h"
#include "chunk.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace PlayerSystem {
    inline void Update(Registry& registry, GLFWwindow* window, const glm::vec3& camFront, const glm::vec3& camUp, float deltaTime) {
        // High-speed compilation group lookup pass
        auto players = registry.QueryGroup<PlayerComponent, TransformComponent, PhysicsComponent>();

        // If no matching player entities exist, return early
        if (players.GetEntities().empty()) return;

        // Pull our target tracking entity index
        Entity playerEntity = players.GetEntities()[0];

        auto& player = registry.GetComponent<PlayerComponent>(playerEntity);
        auto& transform = registry.GetComponent<TransformComponent>(playerEntity);
        auto& physics = registry.GetComponent<PhysicsComponent>(playerEntity);

        // 1. Calculate horizontal movement directions
        glm::vec3 forward = glm::normalize(glm::vec3(camFront.x, 0.0f, camFront.z));
        glm::vec3 right = glm::normalize(glm::cross(camUp, forward));

        glm::vec3 moveIntent(0.0f);
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveIntent += forward;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveIntent -= forward;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveIntent += right;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveIntent -= right;

        if (glm::length(moveIntent) > 0.0f) {
            moveIntent = glm::normalize(moveIntent);
        }

        physics.velocity.x = moveIntent.x * player.movementSpeed;
        physics.velocity.z = moveIntent.z * player.movementSpeed;

        // 2. Sample local heightfield coordinates for collision checks
        const float step = 0.5f;
        float hL = Chunk::GetCachedHeightFromGrid(transform.position.x - step, transform.position.z);
        float hR = Chunk::GetCachedHeightFromGrid(transform.position.x + step, transform.position.z);
        float hD = Chunk::GetCachedHeightFromGrid(transform.position.x, transform.position.z - step);
        float hU = Chunk::GetCachedHeightFromGrid(transform.position.x, transform.position.z + step);

        glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, 2.0f * step, hD - hU));
        glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

        float slopeDot = glm::dot(normal, worldUp);
        float terrainHeight = Chunk::GetCachedHeightFromGrid(transform.position.x, transform.position.z);

        float predictedY = transform.position.y + (physics.velocity.y * deltaTime);

        if (predictedY <= terrainHeight || transform.position.y <= terrainHeight + 0.1f) {
            if (slopeDot < player.minSlopeDot) {
                // Slope is too steep: switch player state to sliding
                physics.isGrounded = false;
                float pushIntoSlope = glm::dot(glm::vec3(physics.velocity.x, 0.0f, physics.velocity.z), glm::vec3(normal.x, 0.0f, normal.z));
                if (pushIntoSlope < 0.0f) {
                    physics.velocity -= normal * glm::dot(physics.velocity, normal);
                }
                glm::vec3 slideDir = glm::normalize(worldUp - normal * slopeDot);
                physics.velocity += slideDir * std::abs(physics.gravity) * 2.0f * deltaTime;

                if (transform.position.y < terrainHeight) {
                    transform.position.y = terrainHeight;
                }
            }
            else {
                // Valid walkable ground: snap player position directly to terrain height
                physics.isGrounded = true;
                transform.position.y = terrainHeight;
                physics.velocity = physics.velocity - normal * glm::dot(physics.velocity, normal);
            }
        }
        else {
            physics.isGrounded = false;
        }

        // 3. Process Physics Forces
        if (!physics.isGrounded) {
            physics.velocity.y += physics.gravity * deltaTime;
        }
        else if (physics.velocity.y < 0.0f) {
            physics.velocity.y = -1.0f; // Soft downward force to keep player glued to hills
        }

        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && physics.isGrounded) {
            physics.velocity.y = physics.jumpForce;
            physics.isGrounded = false;
        }

        // 4. Update relative entity coordinates and dirty the transform state
        transform.position += physics.velocity * deltaTime;
        transform.isDirty = true;
    }
}