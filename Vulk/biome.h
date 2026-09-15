#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "gpu_instances.h"
#include "ecs.h"

enum class BiomeType {
    // Hot
    Desert,
    Savanna,
    Jungle,
    Swamp,

    // Temperate
    Shrubland,
    Plains,
    Forest,
    DeepForest,

    // Cold
    Tundra,
    Taiga,
    SnowWastes,
    Alpine,

    Count
};

struct PropSpawnRule {
    std::string lodGroupName;
    float spawnChance;
    float noiseThreshold;    // Matches against spatial noise to form natural clumps
    int noiseIndex;          // 0 = Forest Noise, 1 = Stone Noise
    float minHeight;
    float maxHeight;
    float minScale;
    float maxScale;
    float groundOffset;
    int maxLod;              // Culling threshold (e.g., 1 for rocks, 3 for trees)
    bool alignToNormal;      // Should the prop tilt with the terrain slope?
};

struct SwarmSpawnRule {
    int boidCount;
    float spawnChance;
    float spreadRadius;
    float minHeight;
    float maxHeight;
    float minScale;
    float maxScale;
    float verticalOffset;
    BoidBehavior behavior; // <-- Added this
};

struct PeripheralProp {
    std::string lodGroupName;
    int minCount;
    int maxCount;
    float minRadius;
    float maxRadius;
    float scaleMin;
    float scaleMax;
};

struct StructureTemplate {
    std::string name;
    std::string centralLodGroup;
    float centralScale;
    std::vector<PeripheralProp> peripherals;
};

struct StructureSpawnRule {
    StructureTemplate structure;
    float spawnChance;
};

struct BiomeDefinition {
    BiomeType type;
    float heightScale;
    float exponent;
    glm::vec3 textureWeights;
    glm::vec3 groundColor;

    float lakeSpawnChance;
    float lakeWaterLevel;
    float lakeMinHeight;
    float lakeMaxHeight;

    std::vector<PropSpawnRule> props;
    std::vector<SwarmSpawnRule> swarms;
    std::vector<StructureSpawnRule> structures;
};

struct PropCollider {
    ColliderType type;
    float baseRadius;
    float baseHeight;
    glm::vec3 baseHalfExtents;
};

// Global Data Access
const BiomeDefinition& GetBiomeDefinition(BiomeType type);
BiomeType DetermineBiome(float temperature, float moisture);

// Wrapper to prevent breaking your existing terrain color generation
inline const BiomeDefinition& GetBiomeProperties(BiomeType type) {
    return GetBiomeDefinition(type);
}

inline const PropCollider& GetPropCollider(const std::string& lodGroupName) {
    static std::unordered_map<std::string, PropCollider> colliders = {
        { "TreeGroup",     { ColliderType::Cylinder, 12.0f, 250.0f, glm::vec3(0.0f) } },
        { "RockGroup",     { ColliderType::Box,      0.0f,    0.0f, glm::vec3(3.0f, 2.0f, 3.0f) } },
        { "TentGroup",     { ColliderType::Box,      0.0f,    0.0f, glm::vec3(5.0f, 4.0f, 5.0f) } },
        { "CampfireGroup", { ColliderType::Sphere,   3.0f,    0.0f, glm::vec3(0.0f) } }
    };

    auto it = colliders.find(lodGroupName);
    if (it != colliders.end()) return it->second;

    static PropCollider defaultCol{ ColliderType::Cylinder, 2.5f, 5.0f, glm::vec3(1.0f) };
    return defaultCol;
}
void InitBiomes();