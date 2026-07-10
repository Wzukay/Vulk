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

#include "assetManager.h"
#include "scene.h"
#include "renderer.h"
#include "threadPool.h"
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
    switch (type) {
        // Smooth transition from flat lowlands to high alpine peaks
    case BiomeType::Plains:         return { 20.0f,  1.0f, glm::vec3(0.0f, 1.0f, 0.0f) };
    case BiomeType::TallPlains:     return { 40.0f,  1.1f, glm::vec3(0.0f, 0.8f, 0.2f) };
    case BiomeType::Foothills:      return { 80.0f,  1.3f, glm::vec3(0.0f, 0.5f, 0.5f) };
    case BiomeType::LowMountain:    return { 100.0f, 1.6f, glm::vec3(0.4f, 0.f, 0.6f) };
    case BiomeType::MediumMountain: return { 150.0f, 2.0f, glm::vec3(0.2f, 0.0f, 0.8f) };
    case BiomeType::HighMountain:   return { 200.0f, 2.5f, glm::vec3(0.0f, 0.0f, 1.0f) };
    }
    return { 40.0f, 1.0f, glm::vec3(0.0f, 1.0f, 0.0f) };
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

struct TerrainSample
{
    float height;
    glm::vec3 biomeWeights;
};

struct RiverSegment {
    std::vector<glm::vec3> path;   // centerline points
    float width;
};

struct ChunkJobResult {
    int cx = 0, cz = 0;
    int64_t key = 0;
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    int lod;

    std::vector<GrassInstance> grassInstances;

    bool hasWater = false;
    WaterMesh waterMesh;

    bool hasRiver = false;
    std::vector<RiverSegment> rivers;
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
    int resolution = 65;
    int viewDistanceChunks = 16;
    int immediateViewChunks = 4;
    static int s_globalSeed;

    static float m_chunkSize;

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

    std::chrono::steady_clock::time_point firstChunkReadyTime;
    std::chrono::steady_clock::time_point lastChunkReadyTime;

    void UpdateFogParamsBasedOnData(VulkanRenderer& renderer);
    glm::vec3 ChunkBoundsCenter(int cx, int cz) const;
    bool HasCameraShiftedNoticeably(const glm::vec3& camPos, const glm::vec3& camForward);
    float ChunkBoundsRadius() const;
    void GenerateChunk(int chunkX, int chunkZ, int resolution, float chunkSize,
        ChunkJobResult& outResult,
        std::shared_ptr<std::atomic<bool>> cancelToken = nullptr);
    static std::vector<glm::vec2> ConvexHull(std::vector<glm::vec2> points);
    static std::pair<float, glm::vec3> CalculateHeightAndColor(float worldX, float worldZ);
	float GetChunkSize() const { return chunkSize; }

public:
    static float GetHeight(float worldX, float worldZ);
};