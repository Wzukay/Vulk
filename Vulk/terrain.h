#pragma once

#include <vector>
#include <memory>
#include <atomic>
#include <cmath>
#include <algorithm>
#include "renderMesh.h"
#include "FastNoiseLite.h"
#include "meshoptimizer.h"

enum class BiomeType {
    Desert,
    Savanna,
    Plains,
    Forest,
    Taiga,
    Tundra
};

struct BiomeProperties {
    float heightScale;
    float exponent;
    glm::vec3 textureWeights;
};

inline BiomeProperties GetBiomeProperties(BiomeType type) {
    switch (type) {
        // Desert is 100% sand texture
    case BiomeType::Desert:  return { 30.0f,  1.2f, glm::vec3(1.0f, 0.0f, 0.0f) };
                          // Savanna is mostly sand/dry dirt mixed with a little grass
    case BiomeType::Savanna: return { 45.0f,  1.5f, glm::vec3(0.6f, 0.4f, 0.0f) };
                           // Plains are 100% grass texture
    case BiomeType::Plains:  return { 25.0f,  2.0f, glm::vec3(0.0f, 1.0f, 0.0f) };
                          // Forest is 100% grass texture
    case BiomeType::Forest:  return { 70.0f,  2.2f, glm::vec3(0.0f, 1.0f, 0.0f) };
                          // Taiga hills start introducing rock/snow textures
    case BiomeType::Taiga:   return { 110.0f, 2.5f, glm::vec3(0.0f, 0.5f, 0.5f) };
                         // Tundra mountains are entirely rock/snow
    case BiomeType::Tundra:  return { 160.0f, 3.0f, glm::vec3(0.0f, 0.0f, 1.0f) };
    }
    return { 50.0f, 2.0f, glm::vec3(0.0f, 1.0f, 0.0f) };
}

inline BiomeType DetermineBiome(float temperature, float moisture) {
    // Cold zones (Temperature < 0.3)
    if (temperature < 0.3f) {
        if (moisture < 0.4f) return BiomeType::Tundra;
        return BiomeType::Taiga;
    }
    // Temperate zones (0.3 <= Temperature < 0.7)
    else if (temperature < 0.7f) {
        if (moisture < 0.3f) return BiomeType::Plains;
        return BiomeType::Forest;
    }
    // Hot zones (Temperature >= 0.7)
    else {
        if (moisture < 0.3f) return BiomeType::Desert;
        return BiomeType::Savanna;
    }
}

class Terrain
{
public:
	static void GenerateChunk(int chunkX, int chunkZ, int resolution, float chunkSize, int seed,
		std::vector<ModelVertex>& outVertices, std::vector<uint32_t>& outIndices,
        std::shared_ptr<std::atomic<bool>> cancelToken);
};

