#pragma once

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>
#include <future>
#include <time.h>
#include <mutex>
#include <memory>
#include <atomic>
#include <algorithm>
#include <queue>
#include <optional>

#include "asset_manager.h"
#include "scene.h"

#include "renderer.h"
#include "renderer_grass.h"

#include "thread_pool.h"
#include "mesh.h"
#include "FastNoiseLite.h"
#include "meshoptimizer.h"

enum class BiomeType {
    Plains,
    TallPlains,
    Foothills,
    LowMountain,
    MediumMountain,
    HighMountain,
};

struct BiomeProperties {
    float heightScale;
    float exponent;
    glm::vec3 textureWeights;
};

inline BiomeProperties GetBiomeProperties(BiomeType type) {
    // Indexed by BiomeType. The old switch had a silent fallback to a
    // default BiomeProperties for any unhandled case — which meant adding a
    // new BiomeType without a matching case would compile fine and just
    // quietly return wrong values. The static_assert below turns that into
    // a compile error instead.
    static const BiomeProperties table[] = {
        { 20.0f,  1.0f, glm::vec3(0.0f, 1.0f, 0.0f) }, // Plains: flat lowlands
        { 40.0f,  1.1f, glm::vec3(0.0f, 0.8f, 0.2f) }, // TallPlains
        { 80.0f,  1.3f, glm::vec3(0.0f, 0.5f, 0.5f) }, // Foothills
        { 100.0f, 1.6f, glm::vec3(0.4f, 0.0f, 0.6f) }, // LowMountain
        { 150.0f, 2.0f, glm::vec3(0.2f, 0.0f, 0.8f) }, // MediumMountain
        { 200.0f, 2.5f, glm::vec3(0.0f, 0.0f, 1.0f) }, // HighMountain: alpine peaks
    };
    static_assert(sizeof(table) / sizeof(table[0]) == static_cast<size_t>(BiomeType::HighMountain) + 1,
        "GetBiomeProperties table must have exactly one entry per BiomeType, in enum order");

    return table[static_cast<size_t>(type)];
}

inline BiomeType DetermineBiome(float temperature, float moisture) {
    // 1. Wet Regions (The Mountain Chains)
    if (moisture >= 0.75f) {
        if (temperature < 0.4f) return BiomeType::HighMountain;    // Cold & Wet = Grand peaks
        if (temperature < 0.7f) return BiomeType::MediumMountain;  // Temperate & Wet
        return BiomeType::LowMountain;                             // Warm & Wet = Low ridges
    }

    // 2. Intermediate Regions (The Highlands / Transition Zones)
    if (moisture >= 0.52f) {
        if (temperature < 0.5f) return BiomeType::MediumMountain;
        if (temperature < 0.75f) return BiomeType::LowMountain;
        return BiomeType::Foothills;
    }

    // 3. Mild Moisture Regions (The Rolling Lowlands)
    if (moisture >= 0.32f) {
        if (temperature < 0.6f) return BiomeType::Foothills;
        return BiomeType::TallPlains;
    }

    // 4. Dry Regions (The Flat Basin Floors)
    if (temperature > 0.65f) return BiomeType::Plains;
    return BiomeType::TallPlains;
}

// A chunk's grid coordinate. Replaces the (cx, cz, key) triple that used to
// be stored separately — and could drift out of sync — in ChunkJobResult,
// ChunkSortItem, and the ad-hoc Key()/Unkey() free functions in chunk.cpp.
// key is always derivable from (cx, cz), so there's only one source of
// truth now.
struct ChunkCoord {
    int cx = 0;
    int cz = 0;

    int64_t Key() const {
        return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cz);
    }
    static ChunkCoord FromKey(int64_t key) {
        return { static_cast<int32_t>(key >> 32), static_cast<int32_t>(key & 0xFFFFFFFF) };
    }
    bool operator==(const ChunkCoord& other) const {
        return cx == other.cx && cz == other.cz;
    }
};

struct TerrainSample
{
    float height;
    glm::vec3 biomeWeights;
};

struct RiverSegment {
    std::vector<glm::vec3> path;   // centerline points
    float width;
};

struct TreeInstance {
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
};

struct ChunkJobResult {
    ChunkCoord coord;
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    int lod;

    std::vector<GrassInstance> grassInstances;
    std::vector<TreeInstance> trees;

    // Present only if this chunk actually generated water/rivers — replaces
    // the old hasWater/waterMesh and hasRiver/rivers bool+data pairs, so
    // there's no separate flag that can fall out of sync with the data.
    //std::optional<WaterMesh> waterMesh;
    //std::optional<std::vector<RiverSegment>> rivers;
};
struct ChunkSortItem {
    ChunkCoord coord;
    float distanceSq;
    int desiredLod;
    int targetResolution;
    bool isImmediate;
};
struct ChunkGridCache {
    int resolution;
    std::vector<float> heightData;
};


struct ActiveJob {
    std::future<ChunkJobResult> future;
    std::shared_ptr<std::atomic<bool>> cancelToken;
    int64_t key;
};

struct PooledMeshBuffers {
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
};
class MeshBufferPool {
private:
    std::mutex m_mutex;
    std::vector<PooledMeshBuffers> m_pool;
public:
    PooledMeshBuffers Acquire() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pool.empty()) {
            return PooledMeshBuffers(); // Fresh allocation only if empty
        }
        auto buffers = std::move(m_pool.back());
        m_pool.pop_back();
        return buffers;
    }

    void Release(PooledMeshBuffers&& buffers) {
        buffers.vertices.clear(); // Clears elements but retains underlying heap capacity
        buffers.indices.clear();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pool.push_back(std::move(buffers));
    }
};

class Chunk {
public:
    Chunk();

    float chunkSize = 512;
    int resolution = 45;
    int viewDistanceChunks = 24;
    int immediateViewChunks = 4;
    static int s_globalSeed;

    static float m_chunkSize;

    std::unordered_map<int64_t, std::future<ChunkJobResult>> m_pendingTerrainGen;

    void Init(VulkanRenderer& renderer);
    void SetSeed(int s) { s_globalSeed = s; }
    void SetRandomSeed() { s_globalSeed = std::rand() % 1000000; }
    bool Update(const glm::vec3& camPos, Scene& scene, VulkanRenderer& renderer);
    void PreGenerateChunks(const glm::vec3& camPos, Scene& scene, VulkanRenderer& renderer);

    static float GetCachedHeightFromGrid(float worldX, float worldZ);
    static void RemoveGridCache(int64_t chunkKey);
    static float GetHeight(float worldX, float worldZ);

    void Shutdown();

private:
    bool m_shuttingDown = false;
    const float MOVE_THRESHOLD = 64.0f;       // Trigger if moved more than 16 meters (e.g., 1/8th of a chunk)
    const float ROTATE_THRESHOLD = 0.965f;

    size_t m_currentAmortizeIndex = 0;
    bool m_needsGridRebuild = false;
    const size_t CHUNKS_PER_FRAME_BUDGET = 32;

    RenderMesh mesh;
    ThreadPool threadPool;
    MeshBufferPool m_bufferPool;

    int m_lastCamChunkX = std::numeric_limits<int>::max();
    int m_lastCamChunkZ = std::numeric_limits<int>::max();

    std::vector<int64_t> m_desiredKeys;
    std::unordered_set<int64_t> m_desiredKeysLookup;
    std::vector<std::pair<int64_t, std::pair<int, int>>> m_desiredList;

    std::unordered_map<int64_t, int> loadedChunks;
    std::unordered_map<int64_t, int> loadingChunks;
    std::vector<ActiveJob> asyncResults;

    glm::vec3 m_lastPreGenCamPos = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 m_lastPreGenCamForward = glm::vec3(0.0f);

    static std::unordered_map<int64_t, ChunkGridCache> s_activeChunkGrids;

    void UpdateFogParamsBasedOnData(VulkanRenderer& renderer);
    glm::vec3 ChunkBoundsCenter(int cx, int cz) const;
    bool HasCameraShiftedNoticeably(const glm::vec3& camPos, const glm::vec3& camForward);
    float ChunkBoundsRadius() const;
    void GenerateChunk(int chunkX, int chunkZ, int resolution, float chunkSize,
        ChunkJobResult& outResult,
        std::shared_ptr<std::atomic<bool>> cancelToken = nullptr);
    static std::vector<glm::vec2> ConvexHull(std::vector<glm::vec2> points);
    static std::pair<float, glm::vec3> CalculateHeightAndColor(float worldX, float worldZ);

    int DesiredLodForDistance(float distance) const;
    void EvictUnloadedChunks(Scene& scene, VulkanRenderer& renderer);

    static uint32_t Hash2D(int x, int z, int seed);
    void GenerateChunkTrees(int chunkX, int chunkZ, int lod, ChunkJobResult& outResult);
};