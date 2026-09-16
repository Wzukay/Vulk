#pragma once

#include "ecs.h"
#include "chunk.h"
#include "settings.h"

#include <cmath>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <limits>

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};
struct RaycastHit {
    Entity entity = INVALID_ENTITY;
    glm::vec3 point = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f);
    float distance = std::numeric_limits<float>::max();
};

class PhysicsSystem {
private:
    static constexpr float CELL_SIZE = 32.0f;
    static inline std::unordered_map<uint64_t, std::vector<Entity>> s_spatialGrid;

    static inline uint64_t GetCellKey(int cx, int cz) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
            static_cast<uint64_t>(static_cast<uint32_t>(cz));
    }

    static bool IntersectRaySphere(const Ray& ray, const glm::vec3& center, float radius, float& outT, glm::vec3& outNormal) {
        glm::vec3 oc = ray.origin - center;
        float b = glm::dot(oc, ray.direction);
        float c = glm::dot(oc, oc) - radius * radius;
        if (c > 0.0f && b > 0.0f) return false;

        float disc = b * b - c;
        if (disc < 0.0f) return false;

        float t = -b - std::sqrt(disc);
        if (t < 0.0f) t = -b + std::sqrt(disc);
        if (t < 0.0f) return false;

        outT = t;
        outNormal = glm::normalize((ray.origin + t * ray.direction) - center);
        return true;
    }

    static bool IntersectRayBox(const Ray& ray, const glm::vec3& center, const glm::vec3& halfExtents, float& outT, glm::vec3& outNormal) {
        glm::vec3 boxMin = center - halfExtents;
        glm::vec3 boxMax = center + halfExtents;

        glm::vec3 safeDir = ray.direction;
        for (int i = 0; i < 3; ++i) {
            if (std::abs(safeDir[i]) < 1e-6f) safeDir[i] = (safeDir[i] >= 0.0f ? 1e-6f : -1e-6f);
        }
        glm::vec3 invDir = 1.0f / safeDir;

        glm::vec3 t0 = (boxMin - ray.origin) * invDir;
        glm::vec3 t1 = (boxMax - ray.origin) * invDir;
        glm::vec3 tMin = glm::min(t0, t1);
        glm::vec3 tMax = glm::max(t0, t1);

        float tNear = std::max(std::max(tMin.x, tMin.y), tMin.z);
        float tFar = std::min(std::min(tMax.x, tMax.y), tMax.z);

        if (tNear > tFar || tFar < 0.0f) return false;

        outT = (tNear < 0.0f) ? tFar : tNear;
        glm::vec3 hitPoint = ray.origin + outT * ray.direction;
        glm::vec3 local = hitPoint - center;

        // Determine hit surface normal
        glm::vec3 normal(0.0f);
        float minDiff = std::numeric_limits<float>::max();
        float dx = halfExtents.x - std::abs(local.x);
        if (dx < minDiff) { minDiff = dx; normal = glm::vec3(local.x > 0 ? 1.0f : -1.0f, 0, 0); }
        float dy = halfExtents.y - std::abs(local.y);
        if (dy < minDiff) { minDiff = dy; normal = glm::vec3(0, local.y > 0 ? 1.0f : -1.0f, 0); }
        float dz = halfExtents.z - std::abs(local.z);
        if (dz < minDiff) { minDiff = dz; normal = glm::vec3(0, 0, local.z > 0 ? 1.0f : -1.0f); }

        outNormal = normal;
        return true;
    }

    static bool IntersectRayCylinder(const Ray& ray, const glm::vec3& baseCenter, float radius, float height, float& outT, glm::vec3& outNormal) {
        float bottomY = baseCenter.y;
        float topY = baseCenter.y + height;

        glm::vec2 rDirXZ(ray.direction.x, ray.direction.z);
        glm::vec2 rOrigXZ(ray.origin.x - baseCenter.x, ray.origin.z - baseCenter.z);

        float a = glm::dot(rDirXZ, rDirXZ);
        float b = glm::dot(rOrigXZ, rDirXZ);
        float c = glm::dot(rOrigXZ, rOrigXZ) - radius * radius;

        float closestT = std::numeric_limits<float>::max();
        glm::vec3 bestNormal(0.0f);
        bool hitFound = false;

        // 1. Curved lateral surface test
        if (a > 1e-6f) {
            float disc = b * b - a * c;
            if (disc >= 0.0f) {
                float sqrtDisc = std::sqrt(disc);
                float tCandidates[2] = { (-b - sqrtDisc) / a, (-b + sqrtDisc) / a };

                for (float t : tCandidates) {
                    if (t > 0.0f && t < closestT) {
                        float hitY = ray.origin.y + t * ray.direction.y;
                        if (hitY >= bottomY && hitY <= topY) {
                            closestT = t;
                            glm::vec3 hitPoint = ray.origin + t * ray.direction;
                            bestNormal = glm::normalize(glm::vec3(hitPoint.x - baseCenter.x, 0.0f, hitPoint.z - baseCenter.z));
                            hitFound = true;
                            break;
                        }
                    }
                }
            }
        }

        // 2. Flat cap tests (bottom & top)
        if (std::abs(ray.direction.y) > 1e-6f) {
            // Bottom Cap
            float tBottom = (bottomY - ray.origin.y) / ray.direction.y;
            if (tBottom > 0.0f && tBottom < closestT) {
                glm::vec3 p = ray.origin + tBottom * ray.direction;
                float distSq = (p.x - baseCenter.x) * (p.x - baseCenter.x) + (p.z - baseCenter.z) * (p.z - baseCenter.z);
                if (distSq <= radius * radius) {
                    closestT = tBottom;
                    bestNormal = glm::vec3(0.0f, -1.0f, 0.0f);
                    hitFound = true;
                }
            }
            // Top Cap
            float tTop = (topY - ray.origin.y) / ray.direction.y;
            if (tTop > 0.0f && tTop < closestT) {
                glm::vec3 p = ray.origin + tTop * ray.direction;
                float distSq = (p.x - baseCenter.x) * (p.x - baseCenter.x) + (p.z - baseCenter.z) * (p.z - baseCenter.z);
                if (distSq <= radius * radius) {
                    closestT = tTop;
                    bestNormal = glm::vec3(0.0f, 1.0f, 0.0f);
                    hitFound = true;
                }
            }
        }

        if (!hitFound) return false;
        outT = closestT;
        outNormal = bestNormal;
        return true;
    }

public:
    static void Update(Registry& registry, float deltaTime) {
        auto physicsGroup = registry.QueryGroup<TransformComponent, PhysicsComponent>();
        auto colliders = registry.QueryGroup<TransformComponent, ColliderComponent>();

        if (physicsGroup.GetEntities().empty()) return;

        glm::vec3 simCenter(0.0f);
        for (Entity physEntity : physicsGroup) {
            if (registry.HasComponent<PlayerComponent>(physEntity)) {
                simCenter = registry.GetComponent<TransformComponent>(physEntity).position;
                break;
            }
        }

        float chunkSize = g_Settings.chunkSize;
        int currentChunkX = static_cast<int>(std::floor(simCenter.x / chunkSize));
        int currentChunkZ = static_cast<int>(std::floor(simCenter.z / chunkSize));

        static int lastChunkX = -999999;
        static int lastChunkZ = -999999;
        static size_t lastColliderCount = SIZE_MAX;

        bool crossedChunkBorder = (currentChunkX != lastChunkX || currentChunkZ != lastChunkZ);
        bool entityCountChanged = (colliders.GetEntities().size() != lastColliderCount);

        if (crossedChunkBorder || entityCountChanged) {
            s_spatialGrid.clear();

            for (Entity colEntity : colliders) {
                const auto& colTransform = registry.GetComponent<TransformComponent>(colEntity);
                const auto& col = registry.GetComponent<ColliderComponent>(colEntity);

                int colChunkX = static_cast<int>(std::floor(colTransform.position.x / chunkSize));
                int colChunkZ = static_cast<int>(std::floor(colTransform.position.z / chunkSize));

                if (std::abs(colChunkX - currentChunkX) > g_Settings.simulationDistance ||
                    std::abs(colChunkZ - currentChunkZ) > g_Settings.simulationDistance) {
                    continue; 
                }

                float boundRadius = col.radius;
                if (col.type == ColliderType::Box) {
                    boundRadius = std::max(col.halfExtents.x, col.halfExtents.z);
                }

                int minCellX = static_cast<int>(std::floor((colTransform.position.x - boundRadius) / CELL_SIZE));
                int maxCellX = static_cast<int>(std::floor((colTransform.position.x + boundRadius) / CELL_SIZE));
                int minCellZ = static_cast<int>(std::floor((colTransform.position.z - boundRadius) / CELL_SIZE));
                int maxCellZ = static_cast<int>(std::floor((colTransform.position.z + boundRadius) / CELL_SIZE));

                for (int cx = minCellX; cx <= maxCellX; ++cx) {
                    for (int cz = minCellZ; cz <= maxCellZ; ++cz) {
                        s_spatialGrid[GetCellKey(cx, cz)].push_back(colEntity);
                    }
                }
            }

            lastChunkX = currentChunkX;
            lastChunkZ = currentChunkZ;
            lastColliderCount = colliders.GetEntities().size();
        }

        static std::vector<Entity> testedEntities;

        for (Entity physEntity : physicsGroup) {
            auto& transform = registry.GetComponent<TransformComponent>(physEntity);
            auto& physics = registry.GetComponent<PhysicsComponent>(physEntity);

            float height = 2.0f;
            float minSlopeDot = 0.7071f;
            float radius = 1.5f;

            if (registry.HasComponent<PlayerComponent>(physEntity)) {
                auto& p = registry.GetComponent<PlayerComponent>(physEntity);
                height = p.playerHeight;
                minSlopeDot = p.minSlopeDot;
            }

            // 1. Terrain & Slope Collision Pass
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
                    float pushIntoSlope = glm::dot(glm::vec3(physics.velocity.x, 0.0f, physics.velocity.z),
                        glm::vec3(normal.x, 0.0f, normal.z));
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
                physics.velocity.y = -1.0f;
            }

            transform.position += physics.velocity * deltaTime;

            // 2. Spatial Grid Broadphase Lookup
            int entityMinCellX = static_cast<int>(std::floor((transform.position.x - radius) / CELL_SIZE));
            int entityMaxCellX = static_cast<int>(std::floor((transform.position.x + radius) / CELL_SIZE));
            int entityMinCellZ = static_cast<int>(std::floor((transform.position.z - radius) / CELL_SIZE));
            int entityMaxCellZ = static_cast<int>(std::floor((transform.position.z + radius) / CELL_SIZE));

            testedEntities.clear();

            for (int cx = entityMinCellX; cx <= entityMaxCellX; ++cx) {
                for (int cz = entityMinCellZ; cz <= entityMaxCellZ; ++cz) {
                    auto it = s_spatialGrid.find(GetCellKey(cx, cz));
                    if (it == s_spatialGrid.end()) continue;

                    for (Entity colEntity : it->second) {
                        if (physEntity == colEntity) continue;

                        // Deduplication: Avoid testing the same prop twice if it spans adjacent cells
                        if (std::find(testedEntities.begin(), testedEntities.end(), colEntity) != testedEntities.end()) {
                            continue;
                        }
                        testedEntities.push_back(colEntity);

                        // 3. Narrowphase Resolution
                        const auto& colTransform = registry.GetComponent<TransformComponent>(colEntity);
                        const auto& col = registry.GetComponent<ColliderComponent>(colEntity);

                        float playerBottom = transform.position.y;
                        float playerTop = transform.position.y + height;
                        float colBottom = colTransform.position.y + col.offset.y;
                        float colTop = colBottom + (col.type == ColliderType::Box ? col.halfExtents.y * 2.0f : col.height);

                        if (playerTop <= colBottom || playerBottom >= colTop) continue;

                        if (col.type == ColliderType::Cylinder || col.type == ColliderType::Sphere) {
                            float dx = transform.position.x - colTransform.position.x;
                            float dz = transform.position.z - colTransform.position.z;
                            float distSq = dx * dx + dz * dz;
                            float minRadius = radius + col.radius;

                            if (distSq < minRadius * minRadius && distSq > 0.0001f) {
                                float dist = std::sqrt(distSq);
                                float overlap = minRadius - dist;
                                transform.position.x += (dx / dist) * overlap;
                                transform.position.z += (dz / dist) * overlap;
                            }
                        }
                        else if (col.type == ColliderType::Box) {
                            glm::vec3 boxMin = colTransform.position - col.halfExtents;
                            glm::vec3 boxMax = colTransform.position + col.halfExtents;

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

            transform.isDirty = true;
        }
    }

    static bool Raycast(Registry& registry, const Ray& ray, RaycastHit& outHit, float maxDistance = 150.0f, Entity ignoreEntity = INVALID_ENTITY) {
        outHit.distance = maxDistance;
        outHit.entity = INVALID_ENTITY;

        if (s_spatialGrid.empty())  return false;

        int cellX = static_cast<int>(std::floor(ray.origin.x / CELL_SIZE));
        int cellZ = static_cast<int>(std::floor(ray.origin.z / CELL_SIZE));

        int stepX = (ray.direction.x > 0.0f) ? 1 : (ray.direction.x < 0.0f ? -1 : 0);
        int stepZ = (ray.direction.z > 0.0f) ? 1 : (ray.direction.z < 0.0f ? -1 : 0);

        float tDeltaX = (stepX != 0) ? std::abs(CELL_SIZE / ray.direction.x) : std::numeric_limits<float>::max();
        float tDeltaZ = (stepZ != 0) ? std::abs(CELL_SIZE / ray.direction.z) : std::numeric_limits<float>::max();

        float nextBoundaryX = (stepX > 0) ? (cellX + 1) * CELL_SIZE : cellX * CELL_SIZE;
        float nextBoundaryZ = (stepZ > 0) ? (cellZ + 1) * CELL_SIZE : cellZ * CELL_SIZE;

        float tMaxX = (stepX != 0) ? (nextBoundaryX - ray.origin.x) / ray.direction.x : std::numeric_limits<float>::max();
        float tMaxZ = (stepZ != 0) ? (nextBoundaryZ - ray.origin.z) / ray.direction.z : std::numeric_limits<float>::max();

        float traversedDistance = 0.0f;
        static std::vector<Entity> queriedEntities;
        queriedEntities.clear();

        bool hitFound = false;

        while (traversedDistance <= maxDistance) {
            auto it = s_spatialGrid.find(GetCellKey(cellX, cellZ));
            if (it != s_spatialGrid.end()) {
                for (Entity colEntity : it->second) {
                    if (colEntity == ignoreEntity) continue;
                    if (std::find(queriedEntities.begin(), queriedEntities.end(), colEntity) != queriedEntities.end()) continue;
                    queriedEntities.push_back(colEntity);

                    const auto& colTransform = registry.GetComponent<TransformComponent>(colEntity);
                    const auto& col = registry.GetComponent<ColliderComponent>(colEntity);
                    glm::vec3 center = colTransform.position + col.offset;

                    float tCandidate = std::numeric_limits<float>::max();
                    glm::vec3 normalCandidate(0.0f);
                    bool isIntersecting = false;

                    switch (col.type) {
                    case ColliderType::Sphere:
                        isIntersecting = IntersectRaySphere(ray, center, col.radius, tCandidate, normalCandidate);
                        break;
                    case ColliderType::Box:
                        isIntersecting = IntersectRayBox(ray, center, col.halfExtents, tCandidate, normalCandidate);
                        break;
                    case ColliderType::Cylinder:
                        isIntersecting = IntersectRayCylinder(ray, center, col.radius, col.height, tCandidate, normalCandidate);
                        break;
                    }

                    if (isIntersecting && tCandidate > 0.0f && tCandidate < outHit.distance) {
                        outHit.distance = tCandidate;
                        outHit.point = ray.origin + tCandidate * ray.direction;
                        outHit.normal = normalCandidate;
                        outHit.entity = colEntity;
                        hitFound = true;
                    }
                }
            }

            // DDA Step
            float minBoundaryT = std::min(tMaxX, tMaxZ);

            // Early exit: if hit is closer than next boundary, no closer hit can exist
            if (hitFound && outHit.distance <= minBoundaryT) {
                break;
            }

            if (tMaxX < tMaxZ) {
                traversedDistance = tMaxX;
                tMaxX += tDeltaX;
                cellX += stepX;
            }
            else {
                traversedDistance = tMaxZ;
                tMaxZ += tDeltaZ;
                cellZ += stepZ;
            }
        }

        return hitFound;
    }
};