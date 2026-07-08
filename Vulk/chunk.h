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

#include "assetManager.h"
#include "scene.h"
#include "renderer.h"
#include "threadPool.h"
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

struct ChunkJobResult {
    int cx = 0, cz = 0;
    int64_t key = 0;
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    int lod;
};

struct ActiveJob {
    std::future<ChunkJobResult> future;
    std::shared_ptr<std::atomic<bool>> cancelToken;
    int64_t key;
};

struct ChunkSortItem {
    int cx, cz;
    int64_t key;
    float distanceSq;
    int desiredLod;
    int targetResolution;
    bool isImmediate;
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
    int resolution = 50;
    int viewDistanceChunks = 50;
    int immediateViewChunks = 12;
    static int s_globalSeed;

    std::unordered_map<int64_t, std::future<ChunkJobResult>> m_pendingTerrainGen;

    void Init(VulkanRenderer& renderer);
    void SetSeed(int s) { s_globalSeed = s; }
    void SetRandomSeed() { s_globalSeed = std::rand() % 1000000; }
    bool Update(const glm::vec3& camPos, Scene& scene, VulkanRenderer& renderer);
    void PreGenerateChunks(const glm::vec3& camPos, Scene& scene, VulkanRenderer& renderer);
    void Shutdown();

private:
    bool m_shuttingDown = false;
    bool batch = false;
    const float MOVE_THRESHOLD = 16.0f;       // Trigger if moved more than 16 meters (e.g., 1/8th of a chunk)
    const float ROTATE_THRESHOLD = 0.965f;

    size_t m_currentAmortizeIndex = 0;
    bool m_needsGridRebuild = false;
    const size_t CHUNKS_PER_FRAME_BUDGET = 16;

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

    std::chrono::steady_clock::time_point firstChunkReadyTime;
    std::chrono::steady_clock::time_point lastChunkReadyTime;

    void UpdateFogParamsBasedOnData(VulkanRenderer& renderer);
    glm::vec3 ChunkBoundsCenter(int cx, int cz) const;
    bool HasCameraShiftedNoticeably(const glm::vec3& camPos, const glm::vec3& camForward);
    float ChunkBoundsRadius() const;
    void GenerateChunk(int chunkX, int chunkZ, int resolution, float chunkSize,
        std::vector<ModelVertex>& outVertices, std::vector<uint32_t>& outIndices,
        std::shared_ptr<std::atomic<bool>> cancelToken = nullptr);

public:
    static float GetHeight(float worldX, float worldZ);
};