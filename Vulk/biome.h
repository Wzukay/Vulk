#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "gpu_instances.h"

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
};

// Global Data Access
const BiomeDefinition& GetBiomeDefinition(BiomeType type);
BiomeType DetermineBiome(float temperature, float moisture);

// Wrapper to prevent breaking your existing terrain color generation
inline const BiomeDefinition& GetBiomeProperties(BiomeType type) {
    return GetBiomeDefinition(type);
}

void InitBiomes();