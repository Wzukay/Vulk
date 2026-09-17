#include "player.h"

Player::Player(glm::vec3 startPos) : position(startPos), velocity(0.0f) {
    // Precalculate the threshold once (e.g., 45 degrees -> ~0.707)
    minSlopeDot = std::cos(glm::radians(maxSlopeAngle));
}

void Player::Update(Registry& registry, Entity playerEntity, GLFWwindow* window, const glm::vec3& camFront, const glm::vec3& camUp, float deltaTime) {
    auto& player = registry.GetComponent<PlayerComponent>(playerEntity);
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

    auto& transform = registry.GetComponent<TransformComponent>(playerEntity);
    float waterLevel = Chunk::GetWaterLevel(transform.position.x, transform.position.z);
    bool inWater = (waterLevel > 0.0f && transform.position.y < waterLevel);

    bool isSprinting = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

    float speed = isSprinting ? player.movementSpeed * 1.6f : player.movementSpeed;

    float waterSpeedPenalty = 1.0f;
    if (inWater) {
        waterSpeedPenalty = isSprinting ? 0.55f : 0.35f;
    }

    speed *= waterSpeedPenalty;

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        if (physics.isGrounded && !inWater) {
            physics.velocity.y = physics.jumpForce;
            physics.isGrounded = false;
        }
        else if (inWater) {
            physics.velocity.y += 40.0f * deltaTime;
        }
    }

    if (inWater && glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
        physics.velocity.y -= 40.0f * deltaTime;
    }

    physics.velocity.x = moveIntent.x * speed;
    physics.velocity.z = moveIntent.z * speed;
}