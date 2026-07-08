#include "chunk.h"
#include <algorithm>
#include <chrono>
#include <unordered_set>

Chunk::Chunk() : threadPool(std::max(1u, std::thread::hardware_concurrency() - 1)) {}

void Chunk::Init(VulkanRenderer& renderer) {
    UpdateFogParamsBasedOnData(renderer);
}

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

    // 🚀 HEIGHT INTEGRATION: Track altitude changes to trigger grid modifications as you fly up/down
    static float lastCamY = camPos.y;
    const float ALTITUDE_REBUILD_THRESHOLD = chunkSize * 0.25f; // Rebuild if camera moves up/down by 1/4 chunk size

    bool cameraMovedXZ = (camChunkX != m_lastCamChunkX || camChunkZ != m_lastCamChunkZ);
    bool cameraMovedY = (std::abs(camPos.y - lastCamY) > ALTITUDE_REBUILD_THRESHOLD);
    bool cameraMoved = cameraMovedXZ || cameraMovedY;

    for (auto it = asyncResults.begin(); it != asyncResults.end(); ) {
        // Correctly using it->future
        if (it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            ChunkJobResult result = it->future.get();

            auto loadingIt = loadingChunks.find(result.key);
            if (loadingIt == loadingChunks.end() || loadingIt->second != result.lod) {
                m_bufferPool.Release({ std::move(result.vertices), std::move(result.indices) });
                it = asyncResults.erase(it);
                continue;
            }

            if (loadedChunks.find(result.key) != loadedChunks.end()) {
                scene.RemoveChunk(result.key);
            }

            renderer.AddTerrainChunk( result.key, result.cx, result.cz,  result.vertices, result.indices);

            loadedChunks[result.key] = result.lod;
            loadingChunks.erase(result.key);

            m_bufferPool.Release({ std::move(result.vertices), std::move(result.indices) });

            it = asyncResults.erase(it);
        }
        else {
            ++it;
        }
    }

    if (cameraMoved) {
        m_lastCamChunkX = camChunkX;
        m_lastCamChunkZ = camChunkZ;
        lastCamY = camPos.y;

        m_desiredKeys.clear();
        m_desiredKeysLookup.clear();
        m_desiredList.clear();

        int range = viewDistanceChunks * 2 + 1;
        m_desiredKeys.reserve(range * range);
        m_desiredList.reserve(range * range);
        m_desiredKeysLookup.reserve(range * range);

        for (int dz = -viewDistanceChunks; dz <= viewDistanceChunks; ++dz) {
            for (int dx = -viewDistanceChunks; dx <= viewDistanceChunks; ++dx) {
                int cx = camChunkX + dx;
                int cz = camChunkZ + dz;
                int64_t k = Key(cx, cz);

                m_desiredKeys.push_back(k);
                m_desiredKeysLookup.insert(k);
                m_desiredList.emplace_back(k, std::make_pair(cx, cz));
            }
        }

        //std::sort(m_desiredKeys.begin(), m_desiredKeys.end());

        m_currentAmortizeIndex = 0;
        m_needsGridRebuild = true;
    }

    if (m_needsGridRebuild && !m_desiredList.empty()) {
        std::vector<ChunkSortItem> chunksToLoadThisFrame;
        size_t processedCount = 0;

        while (processedCount < CHUNKS_PER_FRAME_BUDGET && m_currentAmortizeIndex < m_desiredList.size()) {
            const auto& item = m_desiredList[m_currentAmortizeIndex];
            m_currentAmortizeIndex++;
            processedCount++;

            int64_t key = item.first;
            int cx = item.second.first;
            int cz = item.second.second;

            int dx = cx - camChunkX;
            int dz = cz - camChunkZ;

            glm::vec3 center = ChunkBoundsCenter(cx, cz);

            float deltaX = center.x - camPos.x;
            float deltaY = center.y - camPos.y; // Takes vertical distance into account
            float deltaZ = center.z - camPos.z;
            float distSq = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;

            // Convert to a linear distance for threshold evaluation
            float true3DDistance = std::sqrt(distSq);

            // HEIGHT INTEGRATION: Define LOD ranges based on world units instead of grid units.
            // Adjust these numbers based on your preferences.
            int desiredLod = 0;
            if (true3DDistance <= (chunkSize * 1.5f))    desiredLod = 0;   // was 2.0f
            else if (true3DDistance <= (chunkSize * 3.0f)) desiredLod = 1; // was 4.0f
            else                                          desiredLod = 2;

            int targetResolution = resolution;
            if (desiredLod == 1)      targetResolution = ((resolution - 1) / 2) + 1;
            else if (desiredLod == 2) targetResolution = ((resolution - 1) / 4) + 1;

            if (loadedChunks.find(key) != loadedChunks.end() && loadedChunks[key] <= desiredLod) {
                continue;
            }

            if (loadingChunks.find(key) != loadingChunks.end() && loadingChunks[key] == desiredLod) {
                continue;
            }

            dx = cx - camChunkX;
            dz = cz - camChunkZ;
            bool isImmediateRing = (std::abs(dx) <= immediateViewChunks && std::abs(dz) <= immediateViewChunks);

            if (isImmediateRing) {
                chunksToLoadThisFrame.push_back({ cx, cz, key, distSq, desiredLod, targetResolution, true });
            }
            else {
                float radius = ChunkBoundsRadius();
                
                // Allow chunks slightly further down to load if they cross a 3D radius buffer check
                bool isInCloseRangeCircle = (true3DDistance <= (chunkSize * 5.0f));

                if (!isInCloseRangeCircle && !renderer.IsWorldSphereInFrustum(center, radius)) {
                    continue;
                }

                chunksToLoadThisFrame.push_back({ cx, cz, key, distSq, desiredLod, targetResolution, false });
            }
        }

        if (m_currentAmortizeIndex >= m_desiredList.size()) {
            m_needsGridRebuild = false;
        }

        if (!chunksToLoadThisFrame.empty()) {
            std::sort(chunksToLoadThisFrame.begin(), chunksToLoadThisFrame.end(), [](const ChunkSortItem& a, const ChunkSortItem& b) {
                return a.distanceSq < b.distanceSq;
                });

            auto dispatchChunk = [&](const ChunkSortItem& item) {
                if (loadingChunks.find(item.key) != loadingChunks.end()) {
                    for (auto& activeJob : asyncResults) {
                        if (activeJob.key == item.key) {
                            activeJob.cancelToken->store(true, std::memory_order_relaxed);
                        }
                    }
                }

                loadingChunks[item.key] = item.desiredLod;
                int cx = item.cx; int cz = item.cz; int res = item.targetResolution;
                float size = chunkSize; int s = seed; int64_t k = item.key; int lod = item.desiredLod;

                auto cancelToken = std::make_shared<std::atomic<bool>>(false);

                auto future = threadPool.Enqueue([cx, cz, res, size, s, k, lod, cancelToken, this]() {
                    ChunkJobResult jobData;
                    jobData.cx = cx; jobData.cz = cz; jobData.key = k; jobData.lod = lod;

                    // Acquire clean vectors with pre-allocated memory from the pool
                    PooledMeshBuffers buffers = m_bufferPool.Acquire();
                    jobData.vertices = std::move(buffers.vertices);
                    jobData.indices = std::move(buffers.indices);

                    Terrain::GenerateChunk(cx, cz, res, size, s, jobData.vertices, jobData.indices, cancelToken);
                    return jobData;
                    });

                // Added item.key to match ActiveJob structural properties
                asyncResults.push_back({ std::move(future), cancelToken, item.key });
                };

            size_t distantDispatched = 0;
            const size_t MAX_DISTANT_DISPATCHES_PER_FRAME = 2;

            for (const auto& item : chunksToLoadThisFrame) {
                if (item.isImmediate) {
                    dispatchChunk(item);
                }
                else {
                    if (distantDispatched < MAX_DISTANT_DISPATCHES_PER_FRAME) {
                        dispatchChunk(item);
                        distantDispatched++;
                    }
                }
            }
        }
    }

    for (auto it = loadedChunks.begin(); it != loadedChunks.end(); ) {
        if (m_desiredKeysLookup.find(it->first) == m_desiredKeysLookup.end()) {
            renderer.RemoveTerrainChunk(it->first);
            it = loadedChunks.erase(it);
        }
        else {
            ++it;
        }
    }

    int preGenRadius = viewDistanceChunks + 2;
    for (auto it = loadingChunks.begin(); it != loadingChunks.end(); ) {
        int cx, cz;
        Unkey(it->first, cx, cz);

        int dx = std::abs(cx - camChunkX);
        int dz = std::abs(cz - camChunkZ);

        if (dx > preGenRadius || dz > preGenRadius) {
            int64_t keyToCancel = it->first;
            for (auto& job : asyncResults) {
                if (job.key == keyToCancel) {
                    job.cancelToken->store(true, std::memory_order_relaxed);
                }
            }
            it = loadingChunks.erase(it);
        }
        else {
            ++it;
        }
    }

    return false;
}

void Chunk::PreGenerateChunks(const glm::vec3& camPos, Scene& scene, VulkanRenderer& renderer) {
    if (m_shuttingDown) return;

    glm::vec3 diff = camPos - m_lastPreGenCamPos;
    float distSq = glm::dot(diff, diff);
    if (distSq < (MOVE_THRESHOLD * MOVE_THRESHOLD)) {
        return;
    }
    m_lastPreGenCamPos = camPos;

    int camChunkX = (int)std::floor(camPos.x / chunkSize);
    int camChunkZ = (int)std::floor(camPos.z / chunkSize);
    int preGenRadius = viewDistanceChunks + 2;

    size_t preGenDispatchedThisFrame = 0;
    const size_t MAX_PREGEN_PER_FRAME_BUDGET = 2; // Keep background queues lean

    for (int dz = -preGenRadius; dz <= preGenRadius; ++dz) {
        for (int dx = -preGenRadius; dx <= preGenRadius; ++dx) {

            if (preGenDispatchedThisFrame >= MAX_PREGEN_PER_FRAME_BUDGET) {
                return;
            }

            int cx = camChunkX + dx;
            int cz = camChunkZ + dz;
            int64_t key = Key(cx, cz);

            if (m_desiredKeysLookup.find(key) != m_desiredKeysLookup.end()) {
                continue;
            }

            float chunkDist = std::sqrt((float)(dx * dx + dz * dz));
            int desiredLod = 2;
            if (chunkDist <= 4.0f)      desiredLod = 0;
            else if (chunkDist <= 6.0f) desiredLod = 1;

            int targetResolution = resolution;
            if (desiredLod == 1)      targetResolution = ((resolution - 1) / 2) + 1;
            else if (desiredLod == 2) targetResolution = ((resolution - 1) / 4) + 1;

            if (loadedChunks.find(key) != loadedChunks.end() && loadedChunks[key] <= desiredLod) {
                continue;
            }

            if (loadingChunks.find(key) != loadingChunks.end() && loadingChunks[key] == desiredLod) {
                continue;
            }

            glm::vec3 center = ChunkBoundsCenter(cx, cz);
            float radius = ChunkBoundsRadius();
            if (!renderer.IsWorldSphereInFrustum(center, radius)) {
                continue;
            }

            if (loadingChunks.find(key) != loadingChunks.end()) {
                for (auto& activeJob : asyncResults) {
                    if (activeJob.key == key) {
                        activeJob.cancelToken->store(true, std::memory_order_relaxed);
                    }
                }
            }

            loadingChunks[key] = desiredLod;
            preGenDispatchedThisFrame++;

            auto cancelToken = std::make_shared<std::atomic<bool>>(false);
            float size = chunkSize;
            int s = seed;

            auto future = threadPool.Enqueue([cx, cz, targetResolution, size, s, key, desiredLod, cancelToken, this]() {
                ChunkJobResult jobData;
                jobData.cx = cx;
                jobData.cz = cz;
                jobData.key = key;
                jobData.lod = desiredLod;

                PooledMeshBuffers buffers = m_bufferPool.Acquire();
                jobData.vertices = std::move(buffers.vertices);
                jobData.indices = std::move(buffers.indices);

                Terrain::GenerateChunk(cx, cz, targetResolution, size, s, jobData.vertices, jobData.indices, cancelToken);
                return jobData;
                });

            asyncResults.push_back({ std::move(future), cancelToken, key });
        }
    }
}

bool Chunk::HasCameraShiftedNoticeably(const glm::vec3& camPos, const glm::vec3& camForward) {
    // 1. Check Linear Movement (Squared Distance for performance)
     glm::vec3 diff = camPos - m_lastPreGenCamPos;
     float distSq = glm::dot(diff, diff);

    if (distSq > (MOVE_THRESHOLD * MOVE_THRESHOLD)) {
        return true;
    }

    // 2. Check Angular Movement (Rotation) via Dot Product
    float angleCos = glm::dot(glm::normalize(camForward), glm::normalize(m_lastPreGenCamForward));

    if (angleCos < ROTATE_THRESHOLD) {
        return true;
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

void Chunk::UpdateFogParamsBasedOnData(VulkanRenderer& renderer) {
    float totalViewDistance = viewDistanceChunks * chunkSize;
    float fogStart = 0.3125f * totalViewDistance;   // 1600/5120 = 0.3125
    float fogEnd = 0.3320f * totalViewDistance;   // 1700/5120 ≈ 0.332
    renderer.SetFogParams(fogStart, fogEnd);
}

void Chunk::Shutdown() {
    m_shuttingDown = true;

    threadPool.WaitForAll();

    // Wait for all pending background jobs to complete
    for (auto& job : asyncResults) {
        if (job.future.valid()) {
            job.future.wait();
        }
    }
    asyncResults.clear();
}