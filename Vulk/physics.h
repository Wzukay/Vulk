#pragma once

#include "ecs.h"
#include "chunk.h"
#include <cmath>

class PhysicsSystem {
public:
    static void Update(Registry& registry, float deltaTime) {
        // 1. Fetch all moving entities and all solid colliders
        auto physicsGroup = registry.QueryGroup<TransformComponent, PhysicsComponent>();
        auto colliders = registry.QueryGroup<TransformComponent, ColliderComponent>();

        for (Entity physEntity : physicsGroup) {
            auto& transform = registry.GetComponent<TransformComponent>(physEntity);
            auto& physics = registry.GetComponent<PhysicsComponent>(physEntity);

            // Default physics properties for generic entities
            float height = 2.0f;
            float minSlopeDot = 0.7071f;
            float radius = 1.5f;

            // Override defaults if this entity happens to be a player
            if (registry.HasComponent<PlayerComponent>(physEntity)) {
                auto& p = registry.GetComponent<PlayerComponent>(physEntity);
                height = p.playerHeight;
                minSlopeDot = p.minSlopeDot;
            }

            // 2. Terrain & Gravity Collisions
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

            bool isJumping = physics.velocity.y > 0.0f;

            if (predictedY <= terrainHeight || (!isJumping && transform.position.y <= terrainHeight + 0.1f)) {
                if (slopeDot < minSlopeDot) {
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
                    physics.isGrounded = true;
                    transform.position.y = terrainHeight;
                    physics.velocity = physics.velocity - normal * glm::dot(physics.velocity, normal);
                }
            }
            else {
                physics.isGrounded = false;
            }

            if (!physics.isGrounded) {
                physics.velocity.y += physics.gravity * deltaTime;
            }
            else if (physics.velocity.y < 0.0f) {
                physics.velocity.y = -1.0f; // Soft stickiness
            }

            // Update position based on final processed terrain velocity
            transform.position += physics.velocity * deltaTime;

            // 3. Object-to-Object Collisions
            for (Entity colEntity : colliders) {
                if (physEntity == colEntity) continue;

                const auto& colTransform = registry.GetComponent<TransformComponent>(colEntity);
                const auto& col = registry.GetComponent<ColliderComponent>(colEntity);

                float playerBottom = transform.position.y;
                float playerTop = transform.position.y + height;
                float colBottom = colTransform.position.y + col.offset.y;
                float colTop = colBottom + (col.type == ColliderType::Box ? col.halfExtents.y * 2.0f : 10.0f);

                // Quick vertical bounds check first
                if (playerTop <= colBottom || playerBottom >= colTop) continue;

                if (col.type == ColliderType::Cylinder || col.type == ColliderType::Sphere) {
                    // Cylinder vs Cylinder / Sphere collision
                    float dx = transform.position.x - colTransform.position.x;
                    float dz = transform.position.z - colTransform.position.z;
                    float distSq = dx * dx + dz * dz;
                    float targetRadius = (col.type == ColliderType::Sphere) ? col.radius : col.radius;
                    float minRadius = radius + targetRadius;

                    if (distSq < minRadius * minRadius && distSq > 0.0001f) {
                        float dist = std::sqrt(distSq);
                        float overlap = minRadius - dist;
                        transform.position.x += (dx / dist) * overlap;
                        transform.position.z += (dz / dist) * overlap;
                    }
                }
                else if (col.type == ColliderType::Box) {
                    // Cylinder vs Box (AABB-style push out)
                    glm::vec3 boxMin = colTransform.position - col.halfExtents;
                    glm::vec3 boxMax = colTransform.position + col.halfExtents;

                    // Find closest point on box to player position
                    float closestX = std::clamp(transform.position.x, boxMin.x, boxMax.x);
                    float closestZ = std::clamp(transform.position.z, boxMin.z, boxMax.z);

                    float dx = transform.position.x - closestX;
                    float dz = transform.position.z - closestZ;
                    float distSq = dx * dx + dz * dz;

                    if (distSq < radius * radius && distSq > 0.0001f) {
                        float dist = std::sqrt(distSq);
                        float overlap = radius - dist;
                        transform.position.x += (dx / dist) * overlap;
                        transform.position.z += (dz / dist) * overlap;
                    }
                }
            }
        }
    }
};