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
#include <shared_mutex>

#include "asset_manager.h"
#include "scene.h"

#include "renderer.h"
#include "renderer_grass.h"

#include "thread_pool.h"
#include "mesh.h"
#include "FastNoiseLite.h"
#include "meshoptimizer.h"
#include "biome.h"

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

struct TerrainData {
    float height;
    glm::vec3 biomeWeights; // Used for texture splatting
    glm::vec3 groundColor;  // Used for base ground tint
    float waterLevel;
};

struct TerrainSample
{
    float height;
    glm::vec3 biomeWeights;
};

struct ChunkGridCache {
    int resolution = 0;
    std::vector<float> heightData;
};
struct ChunkJobResult {
    ChunkCoord coord;
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;

    std::vector<ModelVertex> waterVertices;
    std::vector<uint32_t> waterIndices;

    int lod;

    ChunkGridCache physicsGrid;

    std::vector<GrassInstance> grassInstances;
    std::vector<PropInstance> props;
    std::vector<SwarmData> swarms;
};
struct ChunkSortItem {
    ChunkCoord coord;
    float distanceSq;
    int desiredLod;
    int targetResolution;
    bool isImmediate;
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

    static float GetCachedHeightFromGrid(float worldX, float worldZ);
    static void PublishGridCache(int64_t chunkKey, ChunkGridCache&& gridCache);
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

    static std::vector<std::pair<int, int>> s_sortedChunkOffsets;

    std::unordered_map<int64_t, int> loadedChunks;
    std::unordered_map<int64_t, int> loadingChunks;
    std::vector<ActiveJob> asyncResults;

    glm::vec3 m_lastPreGenCamPos = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 m_lastPreGenCamForward = glm::vec3(0.0f);

    static std::unordered_map<int64_t, ChunkGridCache> s_activeChunkGrids;
    static std::shared_mutex s_activeChunkGridsMutex;

    static BiomeType GetDominantBiome(float worldX, float worldZ);

    void UpdateFogParamsBasedOnData(VulkanRenderer& renderer);

    glm::vec3 ChunkBoundsCenter(int cx, int cz) const;
    float ChunkBoundsRadius() const;

    static TerrainData CalculateHeightAndColor(float worldX, float worldZ);
    int DesiredLodForDistance(float distance) const;
    static void PrecomputeChunkOffsets(int viewDistance);
    float GetLodMorph(float closestEdgeDistance, int currentLod) const;

    static uint32_t Hash2D(int x, int z, int seed);


    void GenerateChunk(int chunkX, int chunkZ, int resolution, float chunkSize,
        ChunkJobResult& outResult, std::shared_ptr<std::atomic<bool>> cancelToken = nullptr);

    void GenerateChunkProps(int chunkX, int chunkZ, int lod, ChunkJobResult& outResult,
        const std::function<TerrainData(float, float)>& heightColorFunc);
    void GenerateChunkSwarms(int chunkX, int chunkZ, int lod, ChunkJobResult& outResult,
        const std::function<TerrainData(float, float)>& heightColorFunc);
    void GenerateChunkStructures(int chunkX, int chunkZ, int lod, ChunkJobResult& outResult,
        const std::function<TerrainData(float, float)>& heightColorFunc);
};