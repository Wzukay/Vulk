#include "player.h"
#include "chunk.h"
#include <algorithm>

Player::Player(glm::vec3 startPos) : position(startPos), velocity(0.0f) {
    // Precalculate the threshold once (e.g., 45 degrees -> ~0.707)
    minSlopeDot = std::cos(glm::radians(maxSlopeAngle));
}

void Player::Update(float deltaTime, GLFWwindow* window, const glm::vec3& camFront, const glm::vec3& camUp) {
    // 1. Flatten movement vectors (no flying up using camera angle)
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

    // Apply lateral movement to horizontal velocities
    velocity.x = moveIntent.x * movementSpeed;
    velocity.z = moveIntent.z * movementSpeed;

    // 2. Run Terrain & Slope Collisions BEFORE adding gravity/position
    // This allows us to modify or cancel velocity if we push into a steep wall
    HandleTerrainCollisions(deltaTime);

    // 3. Apply Gravity (if we didn't land on valid ground)
    if (!isGrounded) {
        velocity.y += gravity * deltaTime;
    }
    else if (velocity.y < 0.0f) {
        velocity.y = -1.0f; // Soft downward stickiness force to stay snapped to hills
    }

    // 4. Jump Input
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && isGrounded) {
        velocity.y = jumpForce;
        isGrounded = false;
    }

    // 5. Update Position with final slope-checked velocity
    position += velocity * deltaTime;
}

void Player::HandleTerrainCollisions(float deltaTime) {
    const float step = 0.5f; // Small offset to sample neighboring heights

    // Use fast cached grid queries instead of raw fractal noise!
    float hL = Chunk::GetCachedHeightFromGrid(position.x - step, position.z);
    float hR = Chunk::GetCachedHeightFromGrid(position.x + step, position.z);
    float hD = Chunk::GetCachedHeightFromGrid(position.x, position.z - step);
    float hU = Chunk::GetCachedHeightFromGrid(position.x, position.z + step);

    // Construct the perpendicular surface normal vector
    glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, 2.0f * step, hD - hU));
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    float slopeDot = glm::dot(normal, worldUp);
    float terrainHeight = Chunk::GetCachedHeightFromGrid(position.x, position.z);

    // Predict where the player will be next frame vertically
    float predictedY = position.y + (velocity.y * deltaTime);

    if (predictedY <= terrainHeight || position.y <= terrainHeight + 0.1f) {
        if (slopeDot < minSlopeDot) {
            // SLOPE IS TOO STEEP (Climb Blocked / Sliding)
            isGrounded = false;

            float pushIntoSlope = glm::dot(glm::vec3(velocity.x, 0.0f, velocity.z), glm::vec3(normal.x, 0.0f, normal.z));
            if (pushIntoSlope < 0.0f) {
                velocity -= normal * glm::dot(velocity, normal);
            }

            glm::vec3 slideDir = glm::normalize(worldUp - normal * slopeDot);
            velocity += slideDir * std::abs(gravity) * 2.0f * deltaTime;

            if (position.y < terrainHeight) {
                position.y = terrainHeight;
            }
        }
        else {
            // VALID WALKABLE GROUND
            isGrounded = true;
            position.y = terrainHeight; // Snap precisely to floor
            velocity = velocity - normal * glm::dot(velocity, normal);
        }
    }
    else {
        isGrounded = false; // In mid-air
    }
}