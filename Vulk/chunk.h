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

#include "terrain.h"
#include "assetManager.h"
#include "scene.h"
#include "renderer.h"
#include "threadPool.h"


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

    float chunkSize = 256;
    int resolution = 40;
    int viewDistanceChunks = 12;
    int immediateViewChunks = 4;
    int seed = 0;

    std::unordered_map<int64_t, std::future<ChunkJobResult>> m_pendingTerrainGen;

    void SetSeed(int s) { seed = s; }
    void SetRandomSeed() { seed = std::rand() % 1000000; }
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

    glm::vec3 ChunkBoundsCenter(int cx, int cz) const;
    bool HasCameraShiftedNoticeably(const glm::vec3& camPos, const glm::vec3& camForward);
    float ChunkBoundsRadius() const;
};