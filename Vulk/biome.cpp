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

// --- Biome Data Tables ---
static const BiomeDefinition G_BIOMES[] = {

    // 0: Plains (Flat, dry, no trees, sparse rocks, lots of butterflies)
    {
        .type = BiomeType::Plains,
        .heightScale = 20.0f,
        .exponent = 1.0f,
        .textureWeights = glm::vec3(0.0f, 1.0f, 0.0f),
        .groundColor = glm::vec3(0.48f, 0.58f, 0.24f),
        .props = {
            PropSpawnRule{
                .lodMeshes = ROCK_MESHES,
                .spawnChance = 0.02f,
                .noiseThreshold = 0.7f,
                .noiseIndex = NOISE_INDEX_ROCKS,
                .minHeight = 2.0f,
                .maxHeight = 180.0f,
                .minScale = 0.8f,
                .maxScale = 1.2f,
                .groundOffset = ROCK_SINK_DEPTH,
                .maxLod = CULL_LOD_ROCKS,
                .alignToNormal = true
            }
        },
        .swarms = {
            SwarmSpawnRule{
                .boidCount = 46,
                .spawnChance = 0.40f,
                .spreadRadius = 15.0f,
                .minHeight = 2.0f,
                .maxHeight = 80.0f,
                .minScale = 0.5f,
                .maxScale = 1.0f,
                .verticalOffset = 4.0f
            }
        }
    },

    // 1: TallPlains (Thick grass, occasional tree clumps, butterflies)
    {
        .type = BiomeType::TallPlains,
        .heightScale = 40.0f,
        .exponent = 1.1f,
        .textureWeights = glm::vec3(0.0f, 0.8f, 0.2f),
        .groundColor = glm::vec3(0.35f, 0.49f, 0.18f),
        .props = {
            PropSpawnRule{
                .lodMeshes = TREE_MESHES,
                .spawnChance = 0.05f,
                .noiseThreshold = 0.2f,
                .noiseIndex = NOISE_INDEX_TREES,
                .minHeight = 2.0f,
                .maxHeight = 85.0f,
                .minScale = 0.08f,
                .maxScale = 0.15f,
                .groundOffset = TREE_GROUND_OFFSET,
                .maxLod = CULL_LOD_TREES,
                .alignToNormal = false
            },
            PropSpawnRule{
                .lodMeshes = ROCK_MESHES,
                .spawnChance = 0.03f,
                .noiseThreshold = 0.6f,
                .noiseIndex = NOISE_INDEX_ROCKS,
                .minHeight = 2.0f,
                .maxHeight = 180.0f,
                .minScale = 1.0f,
                .maxScale = 1.4f,
                .groundOffset = ROCK_SINK_DEPTH,
                .maxLod = CULL_LOD_ROCKS,
                .alignToNormal = true
            }
        },
        .swarms = {
            SwarmSpawnRule{
                .boidCount = 46,
                .spawnChance = 0.35f,
                .spreadRadius = 15.0f,
                .minHeight = 2.0f,
                .maxHeight = 80.0f,
                .minScale = 0.5f,
                .maxScale = 1.0f,
                .verticalOffset = 4.0f
            }
        }
    },

    // 2: Foothills (Heavy forests, medium rocks)
    {
        .type = BiomeType::Foothills,
        .heightScale = 80.0f,
        .exponent = 1.3f,
        .textureWeights = glm::vec3(0.0f, 0.5f, 0.5f),
        .groundColor = glm::vec3(0.28f, 0.42f, 0.18f),
        .props = {
            PropSpawnRule{
                .lodMeshes = TREE_MESHES,
                .spawnChance = 0.15f,
                .noiseThreshold = 0.1f,
                .noiseIndex = NOISE_INDEX_TREES,
                .minHeight = 2.0f,
                .maxHeight = 85.0f,
                .minScale = 0.08f,
                .maxScale = 0.15f,
                .groundOffset = TREE_GROUND_OFFSET,
                .maxLod = CULL_LOD_TREES,
                .alignToNormal = false
            },
            PropSpawnRule{
                .lodMeshes = ROCK_MESHES,
                .spawnChance = 0.05f,
                .noiseThreshold = 0.6f,
                .noiseIndex = NOISE_INDEX_ROCKS,
                .minHeight = 2.0f,
                .maxHeight = 180.0f,
                .minScale = 1.0f,
                .maxScale = 1.8f,
                .groundOffset = ROCK_SINK_DEPTH,
                .maxLod = CULL_LOD_ROCKS,
                .alignToNormal = true
            }
        },
        .swarms = {} // Too wooded for swarms
    },

    // 3: LowMountain (Dense forests breaking into rock)
    {
        .type = BiomeType::LowMountain,
        .heightScale = 100.0f,
        .exponent = 1.6f,
        .textureWeights = glm::vec3(0.4f, 0.0f, 0.6f),
        .groundColor = glm::vec3(0.45f, 0.42f, 0.38f),
        .props = {
            PropSpawnRule{
                .lodMeshes = TREE_MESHES,
                .spawnChance = 0.15f,
                .noiseThreshold = 0.1f,
                .noiseIndex = NOISE_INDEX_TREES,
                .minHeight = 2.0f,
                .maxHeight = 85.0f,
                .minScale = 0.08f,
                .maxScale = 0.15f,
                .groundOffset = TREE_GROUND_OFFSET,
                .maxLod = CULL_LOD_TREES,
                .alignToNormal = false
            },
            PropSpawnRule{
                .lodMeshes = ROCK_MESHES,
                .spawnChance = 0.05f,
                .noiseThreshold = 0.5f,
                .noiseIndex = NOISE_INDEX_ROCKS,
                .minHeight = 2.0f,
                .maxHeight = 180.0f,
                .minScale = 1.0f,
                .maxScale = 1.8f,
                .groundOffset = ROCK_SINK_DEPTH,
                .maxLod = CULL_LOD_ROCKS,
                .alignToNormal = true
            }
        },
        .swarms = {}
    },

    // 4: MediumMountain (Above the tree line, heavy rocks)
    {
        .type = BiomeType::MediumMountain,
        .heightScale = 150.0f,
        .exponent = 2.0f,
        .textureWeights = glm::vec3(0.2f, 0.0f, 0.8f),
        .groundColor = glm::vec3(0.40f, 0.40f, 0.42f),
        .props = {
            PropSpawnRule{
                .lodMeshes = ROCK_MESHES,
                .spawnChance = 0.08f,
                .noiseThreshold = 0.4f,
                .noiseIndex = NOISE_INDEX_ROCKS,
                .minHeight = 2.0f,
                .maxHeight = 180.0f,
                .minScale = 1.2f,
                .maxScale = 2.0f,
                .groundOffset = ROCK_SINK_DEPTH,
                .maxLod = CULL_LOD_ROCKS,
                .alignToNormal = true
            }
        },
        .swarms = {}
    },

    // 5: HighMountain (Alpine peaks, snow, giant boulders)
    {
        .type = BiomeType::HighMountain,
        .heightScale = 200.0f,
        .exponent = 2.5f,
        .textureWeights = glm::vec3(0.0f, 0.0f, 1.0f),
        .groundColor = glm::vec3(0.85f, 0.88f, 0.92f),
        .props = {
            PropSpawnRule{
                .lodMeshes = ROCK_MESHES,
                .spawnChance = 0.10f,
                .noiseThreshold = 0.3f,
                .noiseIndex = NOISE_INDEX_ROCKS,
                .minHeight = 2.0f,
                .maxHeight = 180.0f,
                .minScale = 1.5f,
                .maxScale = 2.5f,
                .groundOffset = ROCK_SINK_DEPTH,
                .maxLod = CULL_LOD_ROCKS,
                .alignToNormal = true
            }
        },
        .swarms = {}
    }
};

const BiomeDefinition& GetBiomeDefinition(BiomeType type) {
    return G_BIOMES[static_cast<size_t>(type)];
}

BiomeType DetermineBiome(float temperature, float moisture) {
    if (moisture >= 0.75f) {
        if (temperature < 0.4f) return BiomeType::HighMountain;
        if (temperature < 0.7f) return BiomeType::MediumMountain;
        return BiomeType::LowMountain;
    }
    if (moisture >= 0.52f) {
        if (temperature < 0.5f) return BiomeType::MediumMountain;
        if (temperature < 0.75f) return BiomeType::LowMountain;
        return BiomeType::Foothills;
    }
    if (moisture >= 0.32f) {
        if (temperature < 0.6f) return BiomeType::Foothills;
        return BiomeType::TallPlains;
    }
    if (temperature > 0.65f) return BiomeType::Plains;
    return BiomeType::TallPlains;
}