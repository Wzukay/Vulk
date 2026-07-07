#pragma once

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>
#include <future>
#include <time.h>

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

class Chunk {
public:
    Chunk();

    float chunkSize = 128.0f;
    int resolution = 45;
    int viewDistanceChunks = 8;
    int immediateViewChunks = 2;
    int seed = 0;

    void SetSeed(int s) { seed = s; }
    void SetRandomSeed() { seed = std::rand() % 1000000; }
    bool Update(const glm::vec3& camPos, Scene& scene, VulkanRenderer& renderer);
    void Shutdown();

private:
    bool m_shuttingDown = false;

    RenderMesh mesh;
    ThreadPool threadPool;

    std::vector<int64_t> m_desiredKeys;
    std::vector<std::pair<int64_t, std::pair<int, int>>> m_desiredList;

    int m_lastCamChunkX = std::numeric_limits<int>::max();
    int m_lastCamChunkZ = std::numeric_limits<int>::max();

    std::unordered_map<int64_t, int> loadedChunks;
    std::unordered_map<int64_t, int> loadingChunks;
    std::vector<std::future<ChunkJobResult>> asyncResults;

    bool batch = false;
    std::chrono::steady_clock::time_point firstChunkReadyTime;
    std::chrono::steady_clock::time_point lastChunkReadyTime;

    glm::vec3 ChunkBoundsCenter(int cx, int cz) const;
    float ChunkBoundsRadius() const;
};