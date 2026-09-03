#include "biome.h"
#include <algorithm>

// --- Global Generation Constants ---
constexpr int NOISE_INDEX_TREES = 0;
constexpr int NOISE_INDEX_ROCKS = 1;

constexpr int CULL_LOD_TREES = 3;
constexpr int CULL_LOD_ROCKS = 1;

constexpr float ROCK_SINK_DEPTH = -0.15f;
constexpr float TREE_GROUND_OFFSET = 0.0f;

// --- Reusable Mesh Arrays ---
const std::vector<std::string> TREE_MESHES = {
    "assets/models/tree/tree_lod0.glb",
    "assets/models/tree/tree_lod1.glb",
    "assets/models/tree/tree_lod2.glb",
    "assets/models/tree/tree_lod3.glb"
};

const std::vector<std::string> ROCK_MESHES = {
    "assets/models/rock/rock.glb"
};

// Texture Weights: X = Dirt/Sand, Y = Grass, Z = Rock/Snow
static const BiomeDefinition G_BIOMES[] = {

    // ==========================================
    // HOT BIOMES (Temp > 0.66)
    // ==========================================

    {
        .type = BiomeType::Desert,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(1.0f, 0.0f, 0.0f), // 100% Sand
        .groundColor = glm::vec3(0.76f, 0.69f, 0.50f), // Warm Tan
        .props = {
            PropSpawnRule{ ROCK_MESHES, 0.01f, 0.8f, NOISE_INDEX_ROCKS, 2.0f, 150.0f, 0.5f, 1.2f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
        },
        .swarms = {}
    },
    {
        .type = BiomeType::Savanna,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(0.4f, 0.6f, 0.0f),
        .groundColor = glm::vec3(0.58f, 0.58f, 0.30f), // Dry Yellow-Green
        .props = {
            PropSpawnRule{ TREE_MESHES, 0.02f, 0.4f, NOISE_INDEX_TREES, 2.0f, 60.0f, 0.08f, 0.12f, TREE_GROUND_OFFSET, CULL_LOD_TREES, false },
            PropSpawnRule{ ROCK_MESHES, 0.02f, 0.6f, NOISE_INDEX_ROCKS, 2.0f, 180.0f, 0.8f, 1.5f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
        },
        .swarms = { SwarmSpawnRule{ 25, 0.30f, 20.0f, 2.0f, 60.0f, 0.5f, 0.8f, 4.0f } }
    },
    {
        .type = BiomeType::Jungle,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(0.1f, 0.9f, 0.0f),
        .groundColor = glm::vec3(0.15f, 0.35f, 0.12f), // Deep Vibrant Green
        .props = {
        // High density, variable size trees to simulate canopy
        PropSpawnRule{ TREE_MESHES, 0.30f, 0.0f, NOISE_INDEX_TREES, 2.0f, 90.0f, 0.06f, 0.25f, TREE_GROUND_OFFSET, CULL_LOD_TREES, false }
    },
    .swarms = { SwarmSpawnRule{ 60, 0.50f, 10.0f, 2.0f, 90.0f, 0.3f, 0.6f, 8.0f } }
},
{
    .type = BiomeType::Swamp,
    .heightScale = 0.0f, .exponent = 0.0f,
    .textureWeights = glm::vec3(0.6f, 0.4f, 0.0f),
    .groundColor = glm::vec3(0.25f, 0.28f, 0.18f), // Murky Mud Green
    .props = {
        PropSpawnRule{ TREE_MESHES, 0.10f, 0.3f, NOISE_INDEX_TREES, 2.0f, 30.0f, 0.08f, 0.15f, TREE_GROUND_OFFSET, CULL_LOD_TREES, false },
        PropSpawnRule{ ROCK_MESHES, 0.05f, 0.4f, NOISE_INDEX_ROCKS, 2.0f, 40.0f, 1.0f, 2.0f, -0.5f, CULL_LOD_ROCKS, true } // Sunken rocks
    },
    .swarms = { SwarmSpawnRule{ 80, 0.80f, 8.0f, 2.0f, 30.0f, 0.2f, 0.4f, 2.0f } } // Tons of "flies/bugs"
},

// ==========================================
// TEMPERATE BIOMES (0.33 < Temp <= 0.66)
// ==========================================

{
    .type = BiomeType::Shrubland,
    .heightScale = 0.0f, .exponent = 0.0f,
    .textureWeights = glm::vec3(0.3f, 0.7f, 0.0f),
    .groundColor = glm::vec3(0.48f, 0.58f, 0.24f),
    .props = {
        // Tiny trees acting as bushes
        PropSpawnRule{ TREE_MESHES, 0.08f, 0.5f, NOISE_INDEX_TREES, 2.0f, 80.0f, 0.03f, 0.05f, -0.5f, CULL_LOD_TREES, true },
        PropSpawnRule{ ROCK_MESHES, 0.05f, 0.6f, NOISE_INDEX_ROCKS, 2.0f, 150.0f, 0.5f, 1.0f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
    },
    .swarms = { SwarmSpawnRule{ 30, 0.30f, 15.0f, 2.0f, 80.0f, 0.5f, 1.0f, 4.0f } }
},
{
    .type = BiomeType::Plains,
    .heightScale = 0.0f, .exponent = 0.0f,
    .textureWeights = glm::vec3(0.0f, 1.0f, 0.0f),
    .groundColor = glm::vec3(0.35f, 0.49f, 0.18f), // Lush Green
    .props = {
        PropSpawnRule{ ROCK_MESHES, 0.01f, 0.8f, NOISE_INDEX_ROCKS, 2.0f, 180.0f, 0.8f, 1.2f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
    },
    .swarms = { SwarmSpawnRule{ 46, 0.40f, 15.0f, 2.0f, 80.0f, 0.5f, 1.0f, 4.0f } }
},
{
    .type = BiomeType::Forest,
    .heightScale = 0.0f, .exponent = 0.0f,
    .textureWeights = glm::vec3(0.1f, 0.9f, 0.0f),
    .groundColor = glm::vec3(0.28f, 0.42f, 0.18f),
    .props = {
        PropSpawnRule{ TREE_MESHES, 0.12f, 0.2f, NOISE_INDEX_TREES, 2.0f, 85.0f, 0.08f, 0.14f, TREE_GROUND_OFFSET, CULL_LOD_TREES, false },
        PropSpawnRule{ ROCK_MESHES, 0.05f, 0.5f, NOISE_INDEX_ROCKS, 2.0f, 180.0f, 1.0f, 1.5f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
    },
    .swarms = {}
},
{
    .type = BiomeType::DeepForest,
    .heightScale = 0.0f, .exponent = 0.0f,
    .textureWeights = glm::vec3(0.3f, 0.7f, 0.0f),
    .groundColor = glm::vec3(0.20f, 0.30f, 0.14f), // Very dark green
    .props = {
        PropSpawnRule{ TREE_MESHES, 0.25f, 0.05f, NOISE_INDEX_TREES, 2.0f, 100.0f, 0.10f, 0.18f, TREE_GROUND_OFFSET, CULL_LOD_TREES, false }
    },
    .swarms = {}
},

// ==========================================
// COLD BIOMES (Temp <= 0.33)
// ==========================================

{
    .type = BiomeType::Tundra,
    .heightScale = 0.0f, .exponent = 0.0f,
    .textureWeights = glm::vec3(0.5f, 0.3f, 0.2f),
    .groundColor = glm::vec3(0.45f, 0.42f, 0.38f), // Dead Brown/Grey
    .props = {
        PropSpawnRule{ ROCK_MESHES, 0.05f, 0.4f, NOISE_INDEX_ROCKS, 2.0f, 180.0f, 0.5f, 1.0f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
    },
    .swarms = {}
},
{
    .type = BiomeType::Taiga,
    .heightScale = 0.0f, .exponent = 0.0f,
    .textureWeights = glm::vec3(0.2f, 0.6f, 0.2f),
    .groundColor = glm::vec3(0.32f, 0.38f, 0.32f), // Cold desaturated green
    .props = {
        PropSpawnRule{ TREE_MESHES, 0.15f, 0.2f, NOISE_INDEX_TREES, 2.0f, 100.0f, 0.06f, 0.12f, TREE_GROUND_OFFSET, CULL_LOD_TREES, false },
        PropSpawnRule{ ROCK_MESHES, 0.08f, 0.4f, NOISE_INDEX_ROCKS, 2.0f, 180.0f, 1.0f, 1.5f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
    },
    .swarms = {}
},
{
    .type = BiomeType::SnowWastes,
    .heightScale = 0.0f, .exponent = 0.0f,
    .textureWeights = glm::vec3(0.0f, 0.0f, 1.0f), // 100% Snow/Rock texture
    .groundColor = glm::vec3(0.85f, 0.88f, 0.92f), // Pure Snow
    .props = {
        PropSpawnRule{ ROCK_MESHES, 0.03f, 0.5f, NOISE_INDEX_ROCKS, 2.0f, 200.0f, 1.0f, 2.0f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
    },
    .swarms = {}
},
{
    .type = BiomeType::Alpine,
    .heightScale = 0.0f, .exponent = 0.0f,
    .textureWeights = glm::vec3(0.1f, 0.0f, 0.9f),
    .groundColor = glm::vec3(0.50f, 0.52f, 0.55f), // Grey rock base
    .props = {
        // Massive boulders bridging the landscape
        PropSpawnRule{ ROCK_MESHES, 0.12f, 0.2f, NOISE_INDEX_ROCKS, 2.0f, 250.0f, 1.5f, 4.0f, -0.5f, CULL_LOD_ROCKS, true }
    },
    .swarms = {}
}
};

const BiomeDefinition& GetBiomeDefinition(BiomeType type) {
    return G_BIOMES[static_cast<size_t>(type)];
}

BiomeType DetermineBiome(float temperature, float moisture) {
    // Temperature: 0.0 = Freezing, 1.0 = Scorching
    // Moisture:    0.0 = Bone Dry, 1.0 = Drenched

    if (temperature > 0.66f) {
        // --- HOT ---
        if (moisture < 0.25f) return BiomeType::Desert;
        if (moisture < 0.50f) return BiomeType::Savanna;
        if (moisture < 0.80f) return BiomeType::Jungle;
        return BiomeType::Swamp;
    }
    else if (temperature > 0.33f) {
        // --- TEMPERATE ---
        if (moisture < 0.25f) return BiomeType::Shrubland;
        if (moisture < 0.50f) return BiomeType::Plains;
        if (moisture < 0.80f) return BiomeType::Forest;
        return BiomeType::DeepForest;
    }
    else {
        // --- COLD ---
        if (moisture < 0.30f) return BiomeType::Tundra;
        if (moisture < 0.60f) return BiomeType::Taiga;
        if (moisture < 0.85f) return BiomeType::SnowWastes;
        return BiomeType::Alpine;
    }
}