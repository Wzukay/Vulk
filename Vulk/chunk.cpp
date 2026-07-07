#include "chunk.h"
#include <algorithm>
#include <chrono>
#include <unordered_set>

Chunk::Chunk() : threadPool(std::max(1u, std::thread::hardware_concurrency() - 1)) {}

static int64_t Key(int cx, int cz) {
    return (static_cast<int64_t>(cx) << 32) | (static_cast<uint32_t>(cz));
}
static void Unkey(int64_t key, int& cx, int& cz) {
    cx = static_cast<int32_t>(key >> 32);
    cz = static_cast<int32_t>(key & 0xFFFFFFFF);
}

std::string ChunkName(int cx, int cz) {
    return "terrain_chunk_" + std::to_string(cx) + "_" + std::to_string(cz);
}

bool Chunk::Update(const glm::vec3& camPos, Scene& scene, VulkanRenderer& renderer) {
    if (m_shuttingDown) return false;

    int camChunkX = (int)std::floor(camPos.x / chunkSize);
    int camChunkZ = (int)std::floor(camPos.z / chunkSize);
    bool cameraMoved = (camChunkX != m_lastCamChunkX || camChunkZ != m_lastCamChunkZ);

    if (!cameraMoved) {
        // Still check for finished jobs
        bool anyReady = false;
        for (auto& future : asyncResults) {
            if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                anyReady = true;
                break;
            }
        }
        if (!anyReady) {
            // Nothing to do this frame
            return false;
        }
    }

    // 1. Process any background threads that finished generating terrain data
    for (auto it = asyncResults.begin(); it != asyncResults.end(); ) {
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            ChunkJobResult result = it->get();

            // Safeguard: If the request was cancelled or superseded by an immediate main-thread load
            auto loadingIt = loadingChunks.find(result.key);
            if (loadingIt == loadingChunks.end() || loadingIt->second != result.lod) {
                it = asyncResults.erase(it);
                continue;
            }

            std::string name = ChunkName(result.cx, result.cz);

            // 🟢 HOT-SWAP: If an older LOD tier is currently rendering, strip it from the scene first
            if (loadedChunks.find(result.key) != loadedChunks.end()) {
                scene.RemoveChunk(result.key);
            }

            // Move the generated buffers into the asset manager to avoid copies
            renderer.AddTerrainChunk(
                result.key,
                result.cx,
                result.cz,
                result.vertices,
                result.indices);

            loadedChunks[result.key] = result.lod;
            loadingChunks.erase(result.key);

            it = asyncResults.erase(it);
        }
        else {
            ++it;
        }
    }

    if (camChunkX != m_lastCamChunkX || camChunkZ != m_lastCamChunkZ) {
        m_lastCamChunkX = camChunkX;
        m_lastCamChunkZ = camChunkZ;

        m_desiredKeys.clear();
        m_desiredList.clear();

        int range = viewDistanceChunks * 2 + 1;
        m_desiredKeys.reserve(range * range);
        m_desiredList.reserve(range * range);

        for (int dz = -viewDistanceChunks; dz <= viewDistanceChunks; ++dz) {
            for (int dx = -viewDistanceChunks; dx <= viewDistanceChunks; ++dx) {
                int cx = camChunkX + dx;
                int cz = camChunkZ + dz;
                int64_t k = Key(cx, cz);

                m_desiredKeys.push_back(k);
                m_desiredList.emplace_back(k, std::make_pair(cx, cz));
            }
        }

        std::sort(m_desiredKeys.begin(), m_desiredKeys.end());
    }

    // 3. Gather up potential loading targets and sort them by proximity
    struct ChunkSortItem {
        int cx, cz;
        int64_t key;
        float distanceSq;
        int desiredLod;
        int targetResolution;
    };
    std::vector<ChunkSortItem> chunksToLoad;

    for (const auto& item : m_desiredList) {
        int64_t key = item.first;
        int cx = item.second.first;
        int cz = item.second.second;

        int dx = cx - camChunkX;
        int dz = cz - camChunkZ;
        float chunkDist = std::sqrt((float)(dx * dx + dz * dz));

        // 🟢 DETERMINE LOD TIER
        int desiredLod = 0;
        if (std::abs(dx) <= immediateViewChunks && std::abs(dz) <= immediateViewChunks) {
            desiredLod = 0; // Immediate ring always stays at max quality
        }
        else if (chunkDist <= 2.0f) {
            desiredLod = 0; // High detail up to 3 chunks away
        }
        else if (chunkDist <= 4.0f) {
            desiredLod = 1; // Medium detail
        }
        else {
            desiredLod = 2; // Low detail on the horizon
        }

        // Calculate a safe vertex resolution for this LOD tier
        // Math ensures grid structures align cleanly (e.g., 65 -> 33 -> 17)
        int targetResolution = resolution;
        if (desiredLod == 1)      targetResolution = ((resolution - 1) / 2) + 1;
        else if (desiredLod == 2) targetResolution = ((resolution - 1) / 4) + 1;

        // Check if the current loaded chunk already matches our desired detail level
        auto loadedIt = loadedChunks.find(key);
        if (loadedIt != loadedChunks.end() && loadedIt->second == desiredLod) {
            continue;
        }

        // Check if a background thread is already building this specific LOD tier
        auto loadingIt = loadingChunks.find(key);
        if (loadingIt != loadingChunks.end() && loadingIt->second == desiredLod) {
            continue;
        }

        bool isImmediateRing = (std::abs(dx) <= immediateViewChunks && std::abs(dz) <= immediateViewChunks);

        // HYBRID RULE 1: Handle immediate synchronous rendering
        if (isImmediateRing) {
            glm::vec3 center = ChunkBoundsCenter(cx, cz);
            float deltaX = center.x - camPos.x;
            float deltaZ = center.z - camPos.z;
            float distSq = deltaX * deltaX + deltaZ * deltaZ;

            chunksToLoad.push_back({ cx, cz, key, distSq, desiredLod, targetResolution });
        }
        else {
            glm::vec3 center = ChunkBoundsCenter(cx, cz);
            float radius = ChunkBoundsRadius();

            bool isInCloseRangeCircle = (chunkDist <= 5.0f);

            if (!isInCloseRangeCircle && !renderer.IsWorldSphereInFrustum(center, radius)) {
                continue;
            }

            float deltaX = center.x - camPos.x;
            float deltaZ = center.z - camPos.z;
            float distSq = deltaX * deltaX + deltaZ * deltaZ;

            chunksToLoad.push_back({ cx, cz, key, distSq, desiredLod, targetResolution });
        }
    }

    auto now = std::chrono::steady_clock::now();
    if (!batch) {
        batch = true;
        firstChunkReadyTime = now;
    }
    lastChunkReadyTime = now;

    if (batch) {
        auto msSinceLastReady = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastChunkReadyTime).count();
        auto msSinceFirstReady = std::chrono::duration_cast<std::chrono::milliseconds>(now - firstChunkReadyTime).count();

        // NEW: Trigger instantly if an immediate chunk loaded, otherwise wait for the batch to settle
        if (msSinceLastReady >= 100 || msSinceFirstReady >= 300) {
            batch = false;
        }
    }

    // Sort so closest chunks are at the front
    std::sort(chunksToLoad.begin(), chunksToLoad.end(), [](const ChunkSortItem& a, const ChunkSortItem& b) {
        return a.distanceSq < b.distanceSq;
        });

    const size_t MAX_DISPATCHES_PER_FRAME = 2;
    size_t dispatched = 0;

    for (const auto& item : chunksToLoad) {
        if (dispatched >= MAX_DISPATCHES_PER_FRAME) break;

        loadingChunks[item.key] = item.desiredLod;

        int cx = item.cx;
        int cz = item.cz;
        int res = item.targetResolution;
        float size = chunkSize;
        int s = seed;
        int64_t k = item.key;
        int lod = item.desiredLod;

        asyncResults.push_back(threadPool.Enqueue([cx, cz, res, size, s, k, lod]() {
            ChunkJobResult jobData;
            jobData.cx = cx;
            jobData.cz = cz;
            jobData.key = k;
            jobData.lod = lod;
            Terrain::GenerateChunk(cx, cz, res, size, s, jobData.vertices, jobData.indices);
            return jobData;
            }));

        dispatched++;
    }

    // 4. Clean up and unload chunks out of view range
    for (auto it = loadedChunks.begin(); it != loadedChunks.end(); ) {

        if (!std::binary_search(m_desiredKeys.begin(), m_desiredKeys.end(), it->first)) {
            int cx, cz;
            Unkey(it->first, cx, cz);
            renderer.RemoveTerrainChunk(it->first);
            it = loadedChunks.erase(it);
        }
        else {
            ++it;
        }
    }

    // Clean out processing listings if player ran completely away before they finished
    for (auto it = loadingChunks.begin(); it != loadingChunks.end(); ) {
        if (!std::binary_search(m_desiredKeys.begin(), m_desiredKeys.end(), it->first)) {
            it = loadingChunks.erase(it);
        }
        else {
            ++it;
        }
    }

    return false;
}

glm::vec3 Chunk::ChunkBoundsCenter(int cx, int cz) const {
    float centerX = cx * chunkSize + chunkSize * 0.5f;
    float centerZ = cz * chunkSize + chunkSize * 0.5f;
    return glm::vec3(centerX, 0.0f, centerZ);
}

float Chunk::ChunkBoundsRadius() const {
    float footprintRadius = (chunkSize * 1.41421356f) * 0.5f;
    float heightMargin = 100.0f;
    return std::sqrt(footprintRadius * footprintRadius + heightMargin * heightMargin);
}

void Chunk::Shutdown() {
    m_shuttingDown = true;

    threadPool.WaitForAll();

    // Wait for all pending background jobs to complete
    for (auto& future : asyncResults) {
        if (future.valid()) {
            future.wait();
        }
    }
    asyncResults.clear();
}