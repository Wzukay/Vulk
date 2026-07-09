#include "chunk.h"
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <iostream>

// --- Static seed for height queries ---
int Chunk::s_globalSeed = 23645;

Chunk::Chunk() : threadPool(std::max(1u, std::thread::hardware_concurrency() - 1)) {}

void Chunk::Init(VulkanRenderer& renderer) {
    UpdateFogParamsBasedOnData(renderer);
    renderer.SetTerrainChunkSize(chunkSize);
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

    static float lastCamY = camPos.y;
    const float ALTITUDE_REBUILD_THRESHOLD = chunkSize * 0.25f;

    bool cameraMovedXZ = (camChunkX != m_lastCamChunkX || camChunkZ != m_lastCamChunkZ);
    bool cameraMovedY = (std::abs(camPos.y - lastCamY) > ALTITUDE_REBUILD_THRESHOLD);
    bool cameraMoved = cameraMovedXZ || cameraMovedY;

    // Process completed async jobs
    for (auto it = asyncResults.begin(); it != asyncResults.end(); ) {
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

            renderer.AddTerrainChunk(result.key, result.cx, result.cz, result.lod, result.vertices, result.indices);

            if (result.hasWater) {
                renderer.AddWaterBodyForChunk(
                    result.key,
                    result.waterMesh,
                    "",
                    8.0f,
                    0.15f
                );
            }
            if (result.hasRiver) {
                for (auto& river : result.rivers) {
                    WaterMesh riverMesh = WaterMeshGen::GenerateRiver(river.path, river.width, 4);
                    if (!riverMesh.indices.empty()) {
                        renderer.AddWaterBodyForChunk(result.key, riverMesh, "", 8.0f, 0.15f);
                    }
                }
            }

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
            float deltaY = center.y - camPos.y;
            float deltaZ = center.z - camPos.z;
            float distSq = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
            float true3DDistance = std::sqrt(distSq);

            int desiredLod = 0;
            float morphStart = chunkSize * 1.8f;   // start fading at 80% of chunk size
            float morphEnd = chunkSize * 2.0f;   // finish exactly when LOD1 starts
            if (true3DDistance <= morphStart)           desiredLod = 0;
            else if (true3DDistance <= morphEnd)        desiredLod = 0; // still LOD0, but morphing
            else                                        desiredLod = 1; // LOD1 after morph end

            int targetResolution = resolution;
            if (desiredLod == 1)      targetResolution = ((resolution - 1) / 2) + 1;
            else if (desiredLod == 2) targetResolution = ((resolution - 1) / 4) + 1;

            if (loadedChunks.find(key) != loadedChunks.end() && loadedChunks[key] <= desiredLod)
                continue;
            if (loadingChunks.find(key) != loadingChunks.end() && loadingChunks[key] == desiredLod)
                continue;

            bool isImmediateRing = (std::abs(dx) <= immediateViewChunks && std::abs(dz) <= immediateViewChunks);

            if (isImmediateRing) {
                chunksToLoadThisFrame.push_back({ cx, cz, key, distSq, desiredLod, targetResolution, true });
            }
            else {
                float radius = ChunkBoundsRadius();
                bool isInCloseRangeCircle = (true3DDistance <= (chunkSize * 5.0f));
                if (!isInCloseRangeCircle && !renderer.IsWorldSphereInFrustum(center, radius))
                    continue;
                chunksToLoadThisFrame.push_back({ cx, cz, key, distSq, desiredLod, targetResolution, false });
            }
        }

        if (m_currentAmortizeIndex >= m_desiredList.size())
            m_needsGridRebuild = false;

        if (!chunksToLoadThisFrame.empty()) {
            std::sort(chunksToLoadThisFrame.begin(), chunksToLoadThisFrame.end(),
                [](const ChunkSortItem& a, const ChunkSortItem& b) { return a.distanceSq < b.distanceSq; });

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
                float size = chunkSize;
                int64_t k = item.key;
                int lod = item.desiredLod;

                auto cancelToken = std::make_shared<std::atomic<bool>>(false);

                auto future = threadPool.Enqueue([cx, cz, res, size, k, lod, cancelToken, this]() {
                    ChunkJobResult jobData;
                    jobData.cx = cx;
                    jobData.cz = cz;
                    jobData.key = k;
                    jobData.lod = lod;

                    // Acquire pooled buffers (reuses heap capacity)
                    PooledMeshBuffers buffers = m_bufferPool.Acquire();
                    jobData.vertices = std::move(buffers.vertices);
                    jobData.indices = std::move(buffers.indices);

                    GenerateChunk(cx, cz, res, size, jobData, cancelToken);

                    return jobData;
                    });

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

    // Remove chunks that are no longer desired
    for (auto it = loadedChunks.begin(); it != loadedChunks.end(); ) {
        if (m_desiredKeysLookup.find(it->first) == m_desiredKeysLookup.end()) {
            renderer.RemoveTerrainChunk(it->first);
            renderer.RemoveWaterBody(it->first);
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
    if (distSq < (MOVE_THRESHOLD * MOVE_THRESHOLD)) return;
    m_lastPreGenCamPos = camPos;

    int camChunkX = (int)std::floor(camPos.x / chunkSize);
    int camChunkZ = (int)std::floor(camPos.z / chunkSize);
    int preGenRadius = viewDistanceChunks + 2;

    size_t preGenDispatchedThisFrame = 0;
    const size_t MAX_PREGEN_PER_FRAME_BUDGET = 2;

    for (int dz = -preGenRadius; dz <= preGenRadius; ++dz) {
        for (int dx = -preGenRadius; dx <= preGenRadius; ++dx) {
            if (preGenDispatchedThisFrame >= MAX_PREGEN_PER_FRAME_BUDGET) return;

            int cx = camChunkX + dx;
            int cz = camChunkZ + dz;
            int64_t key = Key(cx, cz);

            if (m_desiredKeysLookup.find(key) != m_desiredKeysLookup.end()) continue;

            float chunkDist = std::sqrt((float)(dx * dx + dz * dz));
            int desiredLod = 2;
            if (chunkDist <= 4.0f)      desiredLod = 0;
            else if (chunkDist <= 6.0f) desiredLod = 1;

            int targetResolution = resolution;
            if (desiredLod == 1)      targetResolution = ((resolution - 1) / 2) + 1;
            else if (desiredLod == 2) targetResolution = ((resolution - 1) / 4) + 1;

            if (loadedChunks.find(key) != loadedChunks.end() && loadedChunks[key] <= desiredLod) continue;
            if (loadingChunks.find(key) != loadingChunks.end() && loadingChunks[key] == desiredLod) continue;

            glm::vec3 center = ChunkBoundsCenter(cx, cz);
            float radius = ChunkBoundsRadius();
            if (!renderer.IsWorldSphereInFrustum(center, radius)) continue;

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
            int s = s_globalSeed;

            auto future = threadPool.Enqueue([cx, cz, targetResolution, size, key, desiredLod, cancelToken, this]() {
                ChunkJobResult jobData;
                jobData.cx = cx;
                jobData.cz = cz;
                jobData.key = key;
                jobData.lod = desiredLod;

                PooledMeshBuffers buffers = m_bufferPool.Acquire();
                jobData.vertices = std::move(buffers.vertices);
                jobData.indices = std::move(buffers.indices);

                GenerateChunk(cx, cz, targetResolution, size, jobData, cancelToken);   // ← same change
                return jobData;
                });

            asyncResults.push_back({ std::move(future), cancelToken, key });
        }
    }
}

void Chunk::GenerateChunk(int chunkX, int chunkZ, int resolution, float chunkSize,
    ChunkJobResult& outResult,
    std::shared_ptr<std::atomic<bool>> cancelToken)
{
    // Clear outputs
    outResult.vertices.clear();
    outResult.indices.clear();
    outResult.hasWater = false;
    outResult.waterMesh.vertices.clear();
    outResult.waterMesh.indices.clear();
    outResult.hasRiver = false;
    outResult.rivers.clear();

    outResult.vertices.reserve(resolution * resolution + (resolution - 1) * 8);
    outResult.indices.reserve(((resolution - 1) * (resolution - 1) * 6) + ((resolution - 1) * 4 * 6));

    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) return;

    const float step = chunkSize / (float)(resolution - 1);
    float originX = chunkX * chunkSize;
    float originZ = chunkZ * chunkSize;

    int gridSize = resolution + 2;
    std::vector<float> heightGrid(gridSize * gridSize);
    std::vector<glm::vec3> colorGrid(gridSize * gridSize);

    // --- Noise setup ---
    static thread_local FastNoiseLite baseNoise;
    static thread_local FastNoiseLite tempNoise;
    static thread_local FastNoiseLite moistNoise;
    static thread_local bool noiseInitialized = false;

    if (!noiseInitialized) {
        baseNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        baseNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        baseNoise.SetFractalOctaves(5);
        baseNoise.SetFrequency(0.002f);

        tempNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        tempNoise.SetFrequency(0.0003f);

        moistNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        moistNoise.SetFrequency(0.0004f);

        noiseInitialized = true;
    }

    int seed = s_globalSeed;
    baseNoise.SetSeed(seed);
    tempNoise.SetSeed(seed + 101);
    moistNoise.SetSeed(seed + 202);

    const float COLD_BOUND = 0.3f;
    const float BLEND_RANGE = 0.5f;

    FastNoiseLite depressionNoise;
    depressionNoise.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
    depressionNoise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Euclidean);
    depressionNoise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance);
    depressionNoise.SetFrequency(0.0015f);
    depressionNoise.SetSeed(seed + 404);

    // Secondary noise for depth & radius variation
    FastNoiseLite depthNoise;
    depthNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    depthNoise.SetFrequency(0.001f);
    depthNoise.SetSeed(seed + 505);

    const float DEPRESSION_THRESHOLD = 0.32f;

    // ---- 1. Generate height and color grid with forced depressions ----
    for (int gz = 0; gz < gridSize; ++gz) {
        float worldZ = originZ + (gz - 1) * step;
        int rowOffset = gz * gridSize;

        for (int gx = 0; gx < gridSize; ++gx) {
            float worldX = originX + (gx - 1) * step;

            // Base biome height
            float rawBase = (baseNoise.GetNoise(worldX, worldZ) + 1.0f) * 0.5f;
            float t = (tempNoise.GetNoise(worldX, worldZ) + 1.0f) * 0.5f;
            float m = (moistNoise.GetNoise(worldX, worldZ) + 1.0f) * 0.5f;

            BiomeProperties properties[4];
            properties[0] = GetBiomeProperties(DetermineBiome(t - BLEND_RANGE, m - BLEND_RANGE));
            properties[1] = GetBiomeProperties(DetermineBiome(t + BLEND_RANGE, m - BLEND_RANGE));
            properties[2] = GetBiomeProperties(DetermineBiome(t - BLEND_RANGE, m + BLEND_RANGE));
            properties[3] = GetBiomeProperties(DetermineBiome(t + BLEND_RANGE, m + BLEND_RANGE));

            float blendedScale = (properties[0].heightScale + properties[1].heightScale + properties[2].heightScale + properties[3].heightScale) * 0.25f;
            float blendedExp = (properties[0].exponent + properties[1].exponent + properties[2].exponent + properties[3].exponent) * 0.25f;
            glm::vec3 blendedWeights = (properties[0].textureWeights + properties[1].textureWeights + properties[2].textureWeights + properties[3].textureWeights) * 0.25f;

            float curvedNoise = std::pow(rawBase, blendedExp);
            float finalHeight = curvedNoise * blendedScale;

            if (t < COLD_BOUND) {
                float coldFactor = 1.0f - (t / COLD_BOUND);
                finalHeight += coldFactor * 20.0f;
            }

            float dist = depressionNoise.GetNoise(worldX, worldZ);
            if (dist < DEPRESSION_THRESHOLD) {
                float d = dist / DEPRESSION_THRESHOLD; // 0..1
                float gaussian = std::exp(-(d * d) / 0.5f); // sigma ~ 0.5
                float depth = 12.0f + depthNoise.GetNoise(worldX * 0.2f + 100, worldZ * 0.2f + 100) * 18.0f;
                depth = std::max(6.0f, std::min(35.0f, depth));
                finalHeight -= depth * gaussian;
            }

            heightGrid[rowOffset + gx] = finalHeight;
            colorGrid[rowOffset + gx] = blendedWeights;
        }
    }

    auto GetCachedHeight = [&](int localX, int localZ) -> float {
        return heightGrid[(localZ + 1) * gridSize + (localX + 1)];
        };
    auto GetCachedColor = [&](int localX, int localZ) -> glm::vec3 {
        return colorGrid[(localZ + 1) * gridSize + (localX + 1)];
        };

    // ---- 2. Vertex assembly ----
    for (int z = 0; z < resolution; z++) {
        if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
            outResult.vertices.clear(); outResult.indices.clear();
            return;
        }
        float worldZ = originZ + z * step;
        for (int x = 0; x < resolution; x++) {
            float worldX = originX + x * step;

            float h = GetCachedHeight(x, z);
            float hL = GetCachedHeight(x - 1, z);
            float hR = GetCachedHeight(x + 1, z);
            float hD = GetCachedHeight(x, z - 1);
            float hU = GetCachedHeight(x, z + 1);

            ModelVertex v;
            v.pos = glm::vec3(worldX, h, worldZ);
            v.texCoord = glm::vec2(worldX * 0.02f, worldZ * 0.02f);
            v.color = GetCachedColor(x, z);

            glm::vec3 tangentX(2.0f * step, hR - hL, 0.0f);
            glm::vec3 tangentZ(0.0f, hU - hD, 2.0f * step);
            v.normal = glm::normalize(glm::cross(tangentZ, tangentX));
            glm::vec3 t = glm::normalize(tangentX);
            t = glm::normalize(t - v.normal * glm::dot(v.normal, t));
            v.tangent = glm::vec4(t, 1.0f);

            float coarseStep = step * 2.0f;
            float coarseX = std::floor(worldX / coarseStep + 0.5f) * coarseStep;
            float coarseZ = std::floor(worldZ / coarseStep + 0.5f) * coarseStep;
            float hCoarse = Chunk::GetHeight(coarseX, coarseZ);
            float hL_coarse = Chunk::GetHeight(coarseX - coarseStep, coarseZ);
            float hR_coarse = Chunk::GetHeight(coarseX + coarseStep, coarseZ);
            float hD_coarse = Chunk::GetHeight(coarseX, coarseZ - coarseStep);
            float hU_coarse = Chunk::GetHeight(coarseX, coarseZ + coarseStep);
            glm::vec3 coarseTangentX(2.0f * coarseStep, hR_coarse - hL_coarse, 0.0f);
            glm::vec3 coarseTangentZ(0.0f, hU_coarse - hD_coarse, 2.0f * coarseStep);
            glm::vec3 coarseNormal = glm::normalize(glm::cross(coarseTangentZ, coarseTangentX));

            v.coarsePos = glm::vec3(coarseX, hCoarse, coarseZ);
            v.coarseNormal = coarseNormal;
            outResult.vertices.push_back(v);
        }
    }

    // ---- 3. Indices ----
    for (int z = 0; z < resolution - 1; z++) {
        for (int x = 0; x < resolution - 1; x++) {
            uint32_t i0 = z * resolution + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = (z + 1) * resolution + x;
            uint32_t i3 = i2 + 1;
            outResult.indices.insert(outResult.indices.end(), { i0, i1, i2, i1, i3, i2 });
        }
    }

    // ---- 4. Skirts ----
    const float skirtDepth = 20.0f;
    auto AddSkirtSegment = [&](uint32_t indexA, uint32_t indexB) {
        uint32_t skirtA = (uint32_t)outResult.vertices.size();
        ModelVertex vA = outResult.vertices[indexA];
        vA.pos.y -= skirtDepth;
        vA.coarsePos = vA.pos;
        vA.coarseNormal = vA.normal;
        outResult.vertices.push_back(vA);

        uint32_t skirtB = (uint32_t)outResult.vertices.size();
        ModelVertex vB = outResult.vertices[indexB];
        vB.pos.y -= skirtDepth;
        vB.coarsePos = vB.pos;
        vB.coarseNormal = vB.normal;
        outResult.vertices.push_back(vB);

        outResult.indices.insert(outResult.indices.end(), { indexA, skirtA, indexB, indexB, skirtA, skirtB });
        };

    for (int x = 0; x < resolution - 1; ++x) AddSkirtSegment(0 * resolution + x, 0 * resolution + (x + 1));
    for (int z = 0; z < resolution - 1; ++z) AddSkirtSegment(z * resolution + (resolution - 1), (z + 1) * resolution + (resolution - 1));
    for (int x = resolution - 1; x > 0; --x) AddSkirtSegment((resolution - 1) * resolution + x, (resolution - 1) * resolution + (x - 1));
    for (int z = resolution - 1; z > 0; --z) AddSkirtSegment(z * resolution + 0, (z - 1) * resolution + 0);

    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
        outResult.vertices.clear(); outResult.indices.clear();
        return;
    }

    // ---- 5. Meshoptimizer ----
    meshopt_optimizeVertexCache(
        outResult.indices.data(),
        outResult.indices.data(),
        outResult.indices.size(),
        outResult.vertices.size()
    );
    std::vector<ModelVertex> rearrangedVertices(outResult.vertices.size());
    meshopt_optimizeVertexFetch(
        rearrangedVertices.data(),
        outResult.indices.data(),
        outResult.indices.size(),
        outResult.vertices.data(),
        outResult.vertices.size(),
        sizeof(ModelVertex)
    );
    outResult.vertices = std::move(rearrangedVertices);

    // ================================================================
    // ---- 6. LAKE generation (basin filling on the modified terrain) ----
    // ================================================================

    const int gW = gridSize;
    const int gH = gridSize;

    auto getH = [&](int x, int y) -> float {
        if (x < 0 || x >= gW || y < 0 || y >= gH) return 1e9f;
        return heightGrid[y * gW + x];
        };

    // 6a. Label basins using steepest descent
    std::vector<int> basinLabels(gW* gH, -1);
    int nextLabel = 0;

    for (int y = 0; y < gH; ++y) {
        for (int x = 0; x < gW; ++x) {
            if (basinLabels[y * gW + x] != -1) continue;

            int cx = x, cy = y;
            while (true) {
                float lowest = getH(cx, cy);
                int nx = cx, ny = cy;
                const int dirs[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
                for (auto d : dirs) {
                    float h = getH(cx + d[0], cy + d[1]);
                    if (h < lowest) {
                        lowest = h;
                        nx = cx + d[0];
                        ny = cy + d[1];
                    }
                }
                if (nx == cx && ny == cy) break;
                if (basinLabels[ny * gW + nx] != -1) {
                    basinLabels[cy * gW + cx] = basinLabels[ny * gW + nx];
                    break;
                }
                cx = nx; cy = ny;
            }
            if (basinLabels[y * gW + x] == -1) {
                basinLabels[y * gW + x] = nextLabel;
                nextLabel++;
            }
        }
    }

    // 6b. Find rim heights for each basin
    std::vector<float> rimMin(gW * gH, 1e9f);
    for (int y = 0; y < gH; ++y) {
        for (int x = 0; x < gW; ++x) {
            int label = basinLabels[y * gW + x];
            if (label == -1) continue;
            const int dirs[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
            for (auto d : dirs) {
                int nx = x + d[0], ny = y + d[1];
                if (nx < 0 || nx >= gW || ny < 0 || ny >= gH) continue;
                int nLabel = basinLabels[ny * gW + nx];
                if (nLabel != label) {
                    float h = getH(nx, ny);
                    if (h < rimMin[y * gW + x]) {
                        rimMin[y * gW + x] = h;
                    }
                }
            }
        }
    }

    // 6c. Compute water level per basin
    std::vector<float> basinWaterLevel(nextLabel, 1e9f);
    for (int y = 0; y < gH; ++y) {
        for (int x = 0; x < gW; ++x) {
            int label = basinLabels[y * gW + x];
            if (label == -1) continue;
            float rim = rimMin[y * gW + x];
            if (rim < basinWaterLevel[label]) {
                basinWaterLevel[label] = rim;
            }
        }
    }

    // 6d. Create lake meshes for basins that are large enough
    for (int label = 0; label < nextLabel; ++label) {
        float waterLevel = basinWaterLevel[label];
        if (waterLevel >= 1e8f) continue;

        std::vector<glm::vec2> cells;
        for (int y = 0; y < gH; ++y) {
            for (int x = 0; x < gW; ++x) {
                if (basinLabels[y * gW + x] == label) {
                    float wx = originX + (x - 1) * step;
                    float wz = originZ + (y - 1) * step;
                    cells.push_back(glm::vec2(wx, wz));
                }
            }
        }

        if (cells.size() < 20) continue; // skip tiny puddles

        std::vector<glm::vec2> hull = ConvexHull(cells);
        if (hull.size() < 3) continue;

        float gridSpacing = step;
        WaterMesh lakeMesh = WaterMeshGen::GenerateLake(hull, waterLevel, gridSpacing);
        if (!lakeMesh.indices.empty()) {
            outResult.hasWater = true;
            outResult.waterMesh = std::move(lakeMesh);
            break; // only one lake per chunk (you can remove `break` to allow multiple)
        }
    }

    // ================================================================
    // ---- 7. RIVER generation ----
    // ================================================================

    const int maxRivers = 2;
    std::vector<RiverSegment> rivers;

    uint32_t rngState = static_cast<uint32_t>(chunkX * 73856093u ^ chunkZ * 19349663u + seed);

    auto randFloat = [&rngState]() -> float {
        rngState = rngState * 1664525u + 1013904223u;
        return (rngState & 0x7FFFFFFFu) / 2147483648.0f;
        };

    for (int attempt = 0; attempt < 20 && rivers.size() < maxRivers; ++attempt) {
        int rx = 1 + (int)(randFloat() * (gW - 2));
        int ry = 1 + (int)(randFloat() * (gH - 2));
        float startH = getH(rx, ry);

        // Check if it's a local maximum (peak)
        bool isPeak = true;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                if (getH(rx + dx, ry + dy) >= startH) {
                    isPeak = false;
                    break;
                }
            }
            if (!isPeak) break;
        }
        if (!isPeak) continue;

        // Trace steepest descent
        std::vector<glm::vec3> path;
        int cx = rx, cy = ry;
        float currentH = startH;
        bool hitLake = false;

        for (int stepCount = 0; stepCount < 200; ++stepCount) {
            path.push_back(glm::vec3(originX + (cx - 1) * step, currentH, originZ + (cy - 1) * step));

            // Check if we reached a lake
            int label = basinLabels[cy * gW + cx];
            if (label != -1 && basinWaterLevel[label] < 1e8f && currentH <= basinWaterLevel[label]) {
                hitLake = true;
                break;
            }

            float lowest = currentH;
            int nx = cx, ny = cy;
            const int dirs[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
            for (auto d : dirs) {
                int tx = cx + d[0], ty = cy + d[1];
                if (tx < 1 || tx >= gW - 1 || ty < 1 || ty >= gH - 1) continue;
                float h = getH(tx, ty);
                if (h < lowest) {
                    lowest = h;
                    nx = tx; ny = ty;
                }
            }
            if (nx == cx && ny == cy) break;
            cx = nx; cy = ny;
            currentH = lowest;
        }

        if (path.size() > 5 && hitLake) {
            RiverSegment seg;
            seg.path = std::move(path);
            seg.width = 2.0f + randFloat() * 4.0f;
            rivers.push_back(std::move(seg));
        }
    }

    if (!rivers.empty()) {
        outResult.hasRiver = true;
        outResult.rivers = std::move(rivers);
    }
}

//TerrainSample Chunk::SampleTerrain(float worldX, float worldZ) {
//
//}

float Chunk::GetHeight(float worldX, float worldZ) {
    static thread_local FastNoiseLite baseNoise;
    static thread_local FastNoiseLite tempNoise;
    static thread_local FastNoiseLite moistNoise;
    static thread_local FastNoiseLite carvingNoise;
    static thread_local FastNoiseLite holeNoise;
    static thread_local bool noiseInitialized = false;

    if (!noiseInitialized) {
        baseNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        baseNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        baseNoise.SetFractalOctaves(5);
        baseNoise.SetFrequency(0.002f);

        tempNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        tempNoise.SetFrequency(0.0003f);

        moistNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        moistNoise.SetFrequency(0.0004f);

        carvingNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        carvingNoise.SetFrequency(0.005f);

        holeNoise.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
        holeNoise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Euclidean);
        holeNoise.SetCellularReturnType(FastNoiseLite::CellularReturnType_CellValue);
        holeNoise.SetFrequency(0.0018f);

        noiseInitialized = true;
    }

    int seed = s_globalSeed;
    baseNoise.SetSeed(seed);
    tempNoise.SetSeed(seed + 101);
    moistNoise.SetSeed(seed + 202);
    carvingNoise.SetSeed(seed + 303);
    holeNoise.SetSeed(seed + 404);

    const float COLD_BOUND = 0.3f;
    const float BLEND_RANGE = 0.5f;

    float rawBase = (baseNoise.GetNoise(worldX, worldZ) + 1.0f) * 0.5f;
    float t = (tempNoise.GetNoise(worldX, worldZ) + 1.0f) * 0.5f;
    float m = (moistNoise.GetNoise(worldX, worldZ) + 1.0f) * 0.5f;

    BiomeProperties props[4];
    props[0] = GetBiomeProperties(DetermineBiome(t - BLEND_RANGE, m - BLEND_RANGE));
    props[1] = GetBiomeProperties(DetermineBiome(t + BLEND_RANGE, m - BLEND_RANGE));
    props[2] = GetBiomeProperties(DetermineBiome(t - BLEND_RANGE, m + BLEND_RANGE));
    props[3] = GetBiomeProperties(DetermineBiome(t + BLEND_RANGE, m + BLEND_RANGE));

    float blendedScale = (props[0].heightScale + props[1].heightScale + props[2].heightScale + props[3].heightScale) * 0.25f;
    float blendedExp = (props[0].exponent + props[1].exponent + props[2].exponent + props[3].exponent) * 0.25f;

    float curvedNoise = std::pow(rawBase, blendedExp);
    float finalHeight = curvedNoise * blendedScale;

    if (t < COLD_BOUND) {
        float coldFactor = 1.0f - (t / COLD_BOUND);
        finalHeight += coldFactor * 20.0f;
    }

    // ---- 1a. Sharp Crevices (Fissures) ----
    float crackSample = std::abs(carvingNoise.GetNoise(worldX, worldZ));
    float creviceDepth = 0.0f;
    if (crackSample < 0.08f) {
        float mask = 1.0f - (crackSample / 0.08f);
        creviceDepth = mask * mask * 28.0f;
    }

    // ---- 1b. Wide Basins/Holes ----
    float holeSample = holeNoise.GetNoise(worldX, worldZ);
    float holeDepth = 0.0f;
    if (holeSample < -0.35f) {
        float mask = (-0.35f - holeSample) / 0.65f;
        holeDepth = mask * mask * 38.0f;
    }

    return finalHeight - (creviceDepth + holeDepth);
}

bool Chunk::HasCameraShiftedNoticeably(const glm::vec3& camPos, const glm::vec3& camForward) {
    glm::vec3 diff = camPos - m_lastPreGenCamPos;
    float distSq = glm::dot(diff, diff);
    if (distSq > (MOVE_THRESHOLD * MOVE_THRESHOLD)) return true;

    float angleCos = glm::dot(glm::normalize(camForward), glm::normalize(m_lastPreGenCamForward));
    if (angleCos < ROTATE_THRESHOLD) return true;

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
    float fogStart = 0.3125f * totalViewDistance;
    float fogEnd = 0.3320f * totalViewDistance;
    renderer.SetFogParams(fogStart, fogEnd);
}

std::vector<glm::vec2> Chunk::ConvexHull(std::vector<glm::vec2> points) {
    if (points.size() <= 3) return points;
    std::sort(points.begin(), points.end(),
        [](const glm::vec2& a, const glm::vec2& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });

    std::vector<glm::vec2> hull;
    // Lower hull
    for (const auto& p : points) {
        while (hull.size() >= 2) {
            const auto& a = hull[hull.size() - 2];
            const auto& b = hull.back();
            if ((b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x) <= 0.0f)
                hull.pop_back();
            else
                break;
        }
        hull.push_back(p);
    }
    // Upper hull
    size_t lower_size = hull.size();
    for (int i = (int)points.size() - 2; i >= 0; --i) {
        const auto& p = points[i];
        while (hull.size() > lower_size) {
            const auto& a = hull[hull.size() - 2];
            const auto& b = hull.back();
            if ((b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x) <= 0.0f)
                hull.pop_back();
            else
                break;
        }
        hull.push_back(p);
    }
    hull.pop_back(); // remove duplicate last point
    return hull;
}

void Chunk::Shutdown() {
    m_shuttingDown = true;
    threadPool.WaitForAll();
    for (auto& job : asyncResults) {
        if (job.future.valid()) {
            job.future.wait();
        }
    }
    asyncResults.clear();
}