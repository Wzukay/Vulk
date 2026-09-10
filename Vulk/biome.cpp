#include "biome.h"
#include <algorithm>
#include "asset_manager.h"

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

// --- BOID BEHAVIOR TEMPLATES ---
BoidBehavior GetButterflyBehavior() {
    BoidBehavior b;
    b.separationRadius = 12.0f; b.alignmentRadius = 0.0f; b.cohesionRadius = 20.0f;
    b.maxSpeed = 15.0f; b.minSpeed = 6.0f; b.turnSpeed = 5.0f; b.wanderStrength = 2.5f;
    b.driftSpeed = 0.8f; b.driftRadius = 150.0f;
    b.animationType = 0;
    b.textureId = 0; // FIX: Deferred until runtime
    b.color1 = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    b.color2 = glm::vec4(1.0f, 0.75f, 0.85f, 1.0f);
    return b;
}

BoidBehavior GetFireflyBehavior() {
    BoidBehavior b;
    b.separationRadius = 8.0f; b.alignmentRadius = 0.0f; b.cohesionRadius = 15.0f;
    b.maxSpeed = 6.0f; b.minSpeed = 2.0f; b.turnSpeed = 3.0f; b.wanderStrength = 4.0f;
    b.driftSpeed = 0.2f; b.driftRadius = 50.0f;
    b.animationType = 1;
    b.textureId = 0; // Procedural glow doesn't need a texture
    b.color1 = glm::vec4(0.5f, 1.0f, 0.2f, 1.0f);
    b.color2 = glm::vec4(0.8f, 1.0f, 0.4f, 1.0f);
    return b;
}

// Texture Weights: X = Dirt/Sand, Y = Grass, Z = Rock/Snow
static BiomeDefinition G_BIOMES[] = {

    // ==========================================
    // HOT BIOMES (Temp > 0.66)
    // ==========================================
    {
        .type = BiomeType::Desert,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(1.0f, 0.0f, 0.0f), // 100% Sand
        .groundColor = glm::vec3(0.76f, 0.69f, 0.50f), // Warm Tan
        .lakeSpawnChance = 0.15f,
        .lakeWaterLevel = 10.0f,
        .lakeMinHeight = 5.0f,
        .lakeMaxHeight = 45.0f,
        .props = {
            PropSpawnRule{ "RockGroup", 0.01f, 0.8f, NOISE_INDEX_ROCKS, 2.0f, 150.0f, 0.5f, 1.2f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
        },
        .swarms = {}
    },
    {
        .type = BiomeType::Savanna,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(0.4f, 0.6f, 0.0f),
        .groundColor = glm::vec3(0.58f, 0.58f, 0.30f), // Dry Yellow-Green
        .lakeSpawnChance = 0.30f,
        .lakeWaterLevel = 12.0f,
        .lakeMinHeight = 5.0f,
        .lakeMaxHeight = 55.0f,
        .props = {
            PropSpawnRule{ "TreeGroup", 0.02f, 0.4f, NOISE_INDEX_TREES, 2.0f, 60.0f, 0.08f, 0.12f, TREE_GROUND_OFFSET, CULL_LOD_TREES, false },
            PropSpawnRule{ "RockGroup", 0.02f, 0.6f, NOISE_INDEX_ROCKS, 2.0f, 180.0f, 0.8f, 1.5f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
        },
        .swarms = { SwarmSpawnRule{ 25, 0.30f, 20.0f, 2.0f, 60.0f, 0.5f, 0.8f, 4.0f, GetButterflyBehavior() } }
    },
    {
        .type = BiomeType::Jungle,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(0.1f, 0.9f, 0.0f),
        .groundColor = glm::vec3(0.15f, 0.35f, 0.12f),
        .lakeSpawnChance = 0.45f,
        .lakeWaterLevel = 14.0f,
        .lakeMinHeight = 5.0f,
        .lakeMaxHeight = 70.0f,
        .props = {
            PropSpawnRule{ "TreeGroup", 0.30f, 0.0f, NOISE_INDEX_TREES, 2.0f, 90.0f, 0.06f, 0.25f, TREE_GROUND_OFFSET, CULL_LOD_TREES, false }
        },
        .swarms = { SwarmSpawnRule{ 60, 0.50f, 10.0f, 2.0f, 90.0f, 0.3f, 0.6f, 8.0f, GetButterflyBehavior() } }
    },
    {
        .type = BiomeType::Swamp,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(0.6f, 0.4f, 0.0f),
        .groundColor = glm::vec3(0.25f, 0.28f, 0.18f),
        .lakeSpawnChance = 0.90f,
        .lakeWaterLevel = 11.0f,
        .lakeMinHeight = 2.0f,
        .lakeMaxHeight = 35.0f,
        .props = {
            PropSpawnRule{ "TreeGroup", 0.10f, 0.3f, NOISE_INDEX_TREES, 2.0f, 30.0f, 0.08f, 0.15f, TREE_GROUND_OFFSET, CULL_LOD_TREES, false },
            PropSpawnRule{ "RockGroup", 0.05f, 0.4f, NOISE_INDEX_ROCKS, 2.0f, 40.0f, 1.0f, 2.0f, -0.5f, CULL_LOD_ROCKS, true }
        },
        .swarms = { SwarmSpawnRule{ 80, 0.80f, 8.0f, 2.0f, 30.0f, 0.2f, 0.4f, 2.0f, GetFireflyBehavior() } }
    },

    // ==========================================
    // TEMPERATE BIOMES (0.33 < Temp <= 0.66)
    // ==========================================
    {
        .type = BiomeType::Shrubland,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(0.3f, 0.7f, 0.0f),
        .groundColor = glm::vec3(0.48f, 0.58f, 0.24f),
        .lakeSpawnChance = 0.25f,
        .lakeWaterLevel = 12.0f,
        .lakeMinHeight = 5.0f,
        .lakeMaxHeight = 55.0f,
        .props = {
            PropSpawnRule{ "TreeGroup", 0.08f, 0.5f, NOISE_INDEX_TREES, 2.0f, 80.0f, 0.03f, 0.05f, -0.5f, CULL_LOD_TREES, true },
            PropSpawnRule{ "RockGroup", 0.05f, 0.6f, NOISE_INDEX_ROCKS, 2.0f, 150.0f, 0.5f, 1.0f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
        },
        .swarms = { SwarmSpawnRule{ 30, 0.30f, 15.0f, 2.0f, 80.0f, 0.5f, 1.0f, 4.0f, GetButterflyBehavior() } }
    },
    {
        .type = BiomeType::Plains,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(0.0f, 1.0f, 0.0f),
        .groundColor = glm::vec3(0.35f, 0.49f, 0.18f),
        .lakeSpawnChance = 0.65f,
        .lakeWaterLevel = 13.0f,
        .lakeMinHeight = 5.0f,
        .lakeMaxHeight = 50.0f,
        .props = {
            PropSpawnRule{ "RockGroup", 0.01f, 0.8f, NOISE_INDEX_ROCKS, 2.0f, 180.0f, 0.8f, 1.2f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
        },
        .swarms = { SwarmSpawnRule{ 46, 0.40f, 15.0f, 2.0f, 80.0f, 0.5f, 1.0f, 4.0f, GetButterflyBehavior() } }
    },
    {
        .type = BiomeType::Forest,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(0.1f, 0.9f, 0.0f),
        .groundColor = glm::vec3(0.28f, 0.42f, 0.18f),
        .lakeSpawnChance = 0.35f,
        .lakeWaterLevel = 13.0f,
        .lakeMinHeight = 5.0f,
        .lakeMaxHeight = 65.0f,
        .props = {
            PropSpawnRule{ "TreeGroup", 0.12f, 0.2f, NOISE_INDEX_TREES, 2.0f, 85.0f, 0.08f, 0.14f, TREE_GROUND_OFFSET, CULL_LOD_TREES, false },
            PropSpawnRule{ "RockGroup", 0.05f, 0.5f, NOISE_INDEX_ROCKS, 2.0f, 180.0f, 1.0f, 1.5f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
        },
        .swarms = { SwarmSpawnRule{ 40, 0.60f, 12.0f, 2.0f, 85.0f, 0.3f, 0.6f, 3.0f, GetFireflyBehavior() } }
    },
    {
        .type = BiomeType::DeepForest,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(0.3f, 0.7f, 0.0f),
        .groundColor = glm::vec3(0.20f, 0.30f, 0.14f),
        .lakeSpawnChance = 0.30f,
        .lakeWaterLevel = 14.0f,
        .lakeMinHeight = 5.0f,
        .lakeMaxHeight = 60.0f,
        .props = {
            PropSpawnRule{ "TreeGroup", 0.25f, 0.05f, NOISE_INDEX_TREES, 2.0f, 100.0f, 0.10f, 0.18f, TREE_GROUND_OFFSET, CULL_LOD_TREES, false }
        },
        .swarms = { SwarmSpawnRule{ 60, 0.80f, 15.0f, 2.0f, 100.0f, 0.3f, 0.6f, 4.0f, GetFireflyBehavior() } }
    },

    // ==========================================
    // COLD BIOMES (Temp <= 0.33)
    // ==========================================
    {
        .type = BiomeType::Tundra,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(0.5f, 0.3f, 0.2f),
        .groundColor = glm::vec3(0.45f, 0.42f, 0.38f),
        .lakeSpawnChance = 0.25f,
        .lakeWaterLevel = 10.0f,
        .lakeMinHeight = 5.0f,
        .lakeMaxHeight = 45.0f,
        .props = {
            PropSpawnRule{ "RockGroup", 0.05f, 0.4f, NOISE_INDEX_ROCKS, 2.0f, 180.0f, 0.5f, 1.0f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
        },
        .swarms = {}
    },
    {
        .type = BiomeType::Taiga,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(0.2f, 0.6f, 0.2f),
        .groundColor = glm::vec3(0.32f, 0.38f, 0.32f),
        .lakeSpawnChance = 0.35f,
        .lakeWaterLevel = 12.0f,
        .lakeMinHeight = 5.0f,
        .lakeMaxHeight = 65.0f,
        .props = {
            PropSpawnRule{ "TreeGroup", 0.15f, 0.2f, NOISE_INDEX_TREES, 2.0f, 100.0f, 0.06f, 0.12f, TREE_GROUND_OFFSET, CULL_LOD_TREES, false },
            PropSpawnRule{ "RockGroup", 0.08f, 0.4f, NOISE_INDEX_ROCKS, 2.0f, 180.0f, 1.0f, 1.5f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
        },
        .swarms = {}
    },
    {
        .type = BiomeType::SnowWastes,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(0.0f, 0.0f, 1.0f),
        .groundColor = glm::vec3(0.85f, 0.88f, 0.92f),
        .lakeSpawnChance = 0.10f,
        .lakeWaterLevel = 11.0f,
        .lakeMinHeight = 5.0f,
        .lakeMaxHeight = 40.0f,
        .props = {
            PropSpawnRule{ "RockGroup", 0.03f, 0.5f, NOISE_INDEX_ROCKS, 2.0f, 200.0f, 1.0f, 2.0f, ROCK_SINK_DEPTH, CULL_LOD_ROCKS, true }
        },
        .swarms = {}
    },
    {
        .type = BiomeType::Alpine,
        .heightScale = 0.0f, .exponent = 0.0f,
        .textureWeights = glm::vec3(0.1f, 0.0f, 0.9f),
        .groundColor = glm::vec3(0.50f, 0.52f, 0.55f),
        .lakeSpawnChance = 0.0f,
        .lakeWaterLevel = 0.0f,
        .lakeMinHeight = 0.0f,
        .lakeMaxHeight = 0.0f,
        .props = {
            PropSpawnRule{ "RockGroup", 0.12f, 0.2f, NOISE_INDEX_ROCKS, 2.0f, 250.0f, 1.5f, 4.0f, -0.5f, CULL_LOD_ROCKS, true }
        },
        .swarms = {}
    }
};

const BiomeDefinition& GetBiomeDefinition(BiomeType type) {
    return G_BIOMES[static_cast<size_t>(type)];
}

BiomeType DetermineBiome(float temperature, float moisture) {
    if (temperature > 0.66f) {
        if (moisture < 0.25f) return BiomeType::Desert;
        if (moisture < 0.50f) return BiomeType::Savanna;
        if (moisture < 0.80f) return BiomeType::Jungle;
        return BiomeType::Swamp;
    }
    else if (temperature > 0.33f) {
        if (moisture < 0.25f) return BiomeType::Shrubland;
        if (moisture < 0.50f) return BiomeType::Plains;
        if (moisture < 0.80f) return BiomeType::Forest;
        return BiomeType::DeepForest;
    }
    else {
        if (moisture < 0.30f) return BiomeType::Tundra;
        if (moisture < 0.60f) return BiomeType::Taiga;
        if (moisture < 0.85f) return BiomeType::SnowWastes;
        return BiomeType::Alpine;
    }
}

void InitBiomes() {
    g_AssetManager.RegisterLodGroup("TreeGroup", TREE_MESHES);
    g_AssetManager.RegisterLodGroup("RockGroup", ROCK_MESHES);

    // Fetch the texture ID safely at runtime
    AssetRecord* record = g_AssetManager.GetAssetRecord("butterfly");
    uint32_t butterflyTexId = record ? record->resourceId : 0;

    // Apply it to all butterfly swarms in the biome definitions
    for (auto& biome : G_BIOMES) {
        for (auto& swarm : biome.swarms) {
            if (swarm.behavior.animationType == 0) { // 0 = Butterfly
                swarm.behavior.textureId = butterflyTexId;
            }
        }
    }
}