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

    // Apply input velocity directly. (Gravity and slope pushing belong to the PhysicsSystem now).
    physics.velocity.x = moveIntent.x * player.movementSpeed;
    physics.velocity.z = moveIntent.z * player.movementSpeed;

    // 2. Process Jump Input (Checks the grounded state determined by last frame's Physics pass)
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && physics.isGrounded) {
        physics.velocity.y = physics.jumpForce;
        physics.isGrounded = false;
    }
}