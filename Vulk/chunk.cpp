#include "chunk.h"
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <iostream>

// --- Static seed for height queries ---
int Chunk::s_globalSeed = 23645;
float Chunk::m_chunkSize = 512.0f;
std::unordered_map<int64_t, ChunkGridCache> Chunk::s_activeChunkGrids;
std::shared_mutex Chunk::s_activeChunkGridsMutex;
std::vector<std::pair<int, int>> Chunk::s_sortedChunkOffsets;

Chunk::Chunk() : threadPool(std::max(1u, std::thread::hardware_concurrency() - 1)) {}

void Chunk::Init(VulkanRenderer& renderer) {
    chunkSize = std::max(16.0f, g_Settings.chunkSize);
    m_chunkSize = chunkSize;

    renderer.SetTerrainChunkSize(chunkSize);
    UpdateFogParamsBasedOnData(renderer);

    PrecomputeChunkOffsets(viewDistanceChunks);
}
uint32_t Chunk::Hash2D(int x, int z, int seed) {
    uint32_t h = static_cast<uint32_t>(seed);
    h ^= static_cast<uint32_t>(x) * 374761393U + static_cast<uint32_t>(z) * 668265263U;
    h = (h ^ (h >> 13)) * 1274126177U;
    return h ^ (h >> 16);
}
static std::string ChunkName(int cx, int cz) {
    return "terrain_chunk_" + std::to_string(cx) + "_" + std::to_string(cz);
}
int Chunk::DesiredLodForDistance(float distToCenter) const {
    // Adds chunk footprint as a safety buffer
    float chunkRadius = chunkSize * 0.75f;
    float closestEdgeDist = std::max(0.0f, distToCenter - chunkRadius);

    // LOD 5: Complete Cull (Past render distance)
    if (closestEdgeDist > g_Settings.renderDistance) return 5;

    // LOD 4: Terrain only, NO Trees (Past tree fade end)
    if (closestEdgeDist > g_Settings.GetStaticFadeEnd()) return 4;

    // LOD 3: Terrain + Billboard Trees (Past tree fade start)
    if (closestEdgeDist > g_Settings.GetStaticFadeStart()) return 3;

    // LOD 2: Terrain + Low Poly Trees, NO Grass (Past grass fade end)
    if (closestEdgeDist > g_Settings.GetGrassFadeEnd()) return 2;

    // LOD 1: Terrain + Med Trees + Thin Grass (Past grass fade start)
    if (closestEdgeDist > g_Settings.GetGrassFadeStart()) return 1;

    // LOD 0: High Poly Everything
    return 0;
}
void Chunk::PrecomputeChunkOffsets(int viewDistance) {
    if (!s_sortedChunkOffsets.empty()) return;

    for (int dz = -viewDistance; dz <= viewDistance; ++dz) {
        for (int dx = -viewDistance; dx <= viewDistance; ++dx) {
            s_sortedChunkOffsets.push_back({ dx, dz });
        }
    }

    // Sort once based on squared distance from center
    std::sort(s_sortedChunkOffsets.begin(), s_sortedChunkOffsets.end(), [](const auto& a, const auto& b) {
        return (a.first * a.first + a.second * a.second) < (b.first * b.first + b.second * b.second);
        });
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

bool Chunk::Update(const glm::vec3& camPos, Scene& scene, VulkanRenderer& renderer) {
    if (m_shuttingDown) return false;

    if (!loadedChunks.empty() && !m_needsGridRebuild) {
        static size_t lodCheckIndex = 0;
        auto it = loadedChunks.begin();
        std::advance(it, lodCheckIndex % loadedChunks.size());
        lodCheckIndex++;

        ChunkCoord coord = ChunkCoord::FromKey(it->first);
        glm::vec3 center = ChunkBoundsCenter(coord.cx, coord.cz);
        float distanceToCam = glm::distance(center, camPos);
        int desiredLod = DesiredLodForDistance(distanceToCam);

        if (it->second > desiredLod) {
            m_needsGridRebuild = true;
            m_currentAmortizeIndex = 0;
        }
    }

    static int frameCounter = 0;
    bool shouldCleanup = false;

    int camChunkX = (int)std::floor(camPos.x / chunkSize);
    int camChunkZ = (int)std::floor(camPos.z / chunkSize);

    static float lastCamY = camPos.y;
    const float ALTITUDE_REBUILD_THRESHOLD = chunkSize * 0.25f;

    bool cameraMovedXZ = (camChunkX != m_lastCamChunkX || camChunkZ != m_lastCamChunkZ);
    bool cameraMovedY = (std::abs(camPos.y - lastCamY) > ALTITUDE_REBUILD_THRESHOLD);
    shouldCleanup = cameraMovedXZ || cameraMovedY;

    int processedThisFrame = 0;

    // Process completed async jobs
    for (auto it = asyncResults.begin(); it != asyncResults.end(); ) {
        if (processedThisFrame >= 1) {
            break;
        }

        if (it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            ChunkJobResult result = it->future.get();
            int64_t resultKey = result.coord.Key();

            auto loadingIt = loadingChunks.find(resultKey);
            if (loadingIt == loadingChunks.end() || loadingIt->second != result.lod) {
                m_bufferPool.Release({ std::move(result.vertices), std::move(result.indices) });
                it = asyncResults.erase(it);
                continue;
            }

            if (loadedChunks.find(resultKey) != loadedChunks.end()) {
                scene.RemoveChunk(resultKey);
            }

            bool staticSceneChanged = false;

            for (const auto& propData : result.props) {
                Entity propEntity = scene.GetRegistry().CreateEntity();

                TransformComponent tComp;
                tComp.position = propData.position;
                tComp.rotation = propData.rotation;
                tComp.scale = propData.scale;
                tComp.isDirty = true;

                RenderComponent rComp;

                if (!propData.lodMeshes.empty()) {
                    int targetLodIndex = std::min(result.lod, static_cast<int>(propData.lodMeshes.size()) - 1);
                    rComp.meshName = propData.lodMeshes[targetLodIndex];
                }
                else {
                    rComp.meshName = ""; // Fallback
                }

                rComp.type = MeshType::Static;
                rComp.isInstanced = true;
                rComp.isVisible = true;

                scene.GetRegistry().AddComponent<TransformComponent>(propEntity, tComp);
                scene.GetRegistry().AddComponent<RenderComponent>(propEntity, rComp);

                ChunkPropComponent cComp;
                cComp.chunkKey = resultKey;
                scene.GetRegistry().AddComponent<ChunkPropComponent>(propEntity, cComp);

                staticSceneChanged = true;
            }

            if (staticSceneChanged) scene.MarkDirty();

            PublishGridCache(resultKey, std::move(result.physicsGrid));

            renderer.AddTerrainChunk(resultKey, result.coord.cx, result.coord.cz, result.lod, result.vertices, result.indices);
            
            if (!result.grassInstances.empty()) {
                renderer.AddGrass(resultKey, result.grassInstances);
            }
            else {
                renderer.RemoveGrass(resultKey);
            }

            if (!result.boids.empty()) {
                renderer.AddBoid(resultKey, result.boids, 3);
            }

            loadedChunks[resultKey] = result.lod;
            loadingChunks.erase(resultKey);
            m_bufferPool.Release({ std::move(result.vertices), std::move(result.indices) });

            processedThisFrame++;
            it = asyncResults.erase(it);
        }
        else {
            ++it;
        }
    }

    if (++frameCounter > 60) {
        frameCounter = 0;
        shouldCleanup = true;
    }

    if (shouldCleanup) {
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

        // FIX 1: O(1) grid generation using the precomputed spiral offset. 
        // No std::sort required!
        for (const auto& offset : s_sortedChunkOffsets) {
            ChunkCoord coord{ camChunkX + offset.first, camChunkZ + offset.second };
            int64_t k = coord.Key();
            m_desiredKeys.push_back(k);
            m_desiredKeysLookup.insert(k);
            m_desiredList.emplace_back(k, std::make_pair(coord.cx, coord.cz));
        }

        m_currentAmortizeIndex = 0;
        m_needsGridRebuild = true;

        // FIX 2: MOVED FROM BOTTOM OF FUNCTION
        // Only run the heavy O(N) map deletion loops when the grid actually shifts!
        for (auto it = loadedChunks.begin(); it != loadedChunks.end(); ) {
            if (m_desiredKeysLookup.find(it->first) == m_desiredKeysLookup.end()) {
                RemoveGridCache(it->first);
                renderer.RemoveTerrainChunk(it->first);
                renderer.RemoveGrass(it->first);
                renderer.RemoveBoid(it->first);
                scene.RemoveChunk(it->first);
                it = loadedChunks.erase(it);
            }
            else {
                ++it;
            }
        }

        int preGenRadius = viewDistanceChunks + 2;
        for (auto it = loadingChunks.begin(); it != loadingChunks.end(); ) {
            ChunkCoord coord = ChunkCoord::FromKey(it->first);
            int dx = std::abs(coord.cx - camChunkX);
            int dz = std::abs(coord.cz - camChunkZ);
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
    }

    if (m_needsGridRebuild && !m_desiredList.empty()) {
        std::vector<ChunkSortItem> chunksToLoadThisFrame;
        size_t processedCount = 0;

        while (processedCount < CHUNKS_PER_FRAME_BUDGET && m_currentAmortizeIndex < m_desiredList.size()) {
            const auto& item = m_desiredList[m_currentAmortizeIndex];
            m_currentAmortizeIndex++;
            processedCount++;

            int64_t key = item.first;
            ChunkCoord coord{ item.second.first, item.second.second };
            int cx = coord.cx;
            int cz = coord.cz;

            int dx = cx - camChunkX;
            int dz = cz - camChunkZ;

            glm::vec3 center = ChunkBoundsCenter(cx, cz);
            float deltaX = center.x - camPos.x;
            float deltaY = center.y - camPos.y;
            float deltaZ = center.z - camPos.z;
            float distSq = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
            float true3DDistance = std::sqrt(distSq);

            int desiredLod = DesiredLodForDistance(true3DDistance);

            int divisor = 1 << desiredLod;
            int targetResolution = ((resolution - 1) / divisor) + 1;

            if (loadedChunks.find(key) != loadedChunks.end()) {
                int currentLod = loadedChunks[key];
                if (currentLod == desiredLod) {
                    continue;
                }
            }
            if (loadingChunks.find(key) != loadingChunks.end() && loadingChunks[key] == desiredLod)
                continue;

            bool isImmediateRing = (std::abs(dx) <= immediateViewChunks && std::abs(dz) <= immediateViewChunks);
            if (isImmediateRing) {
                chunksToLoadThisFrame.push_back({ coord, distSq, desiredLod, targetResolution, true });
            }
            else {
                float radius = ChunkBoundsRadius();
                bool isInCloseRangeCircle = (true3DDistance <= (chunkSize * 5.0f));

                if (!isInCloseRangeCircle && !renderer.IsWorldSphereInFrustum(center, radius))
                    continue;

                chunksToLoadThisFrame.push_back({ coord, distSq, desiredLod, targetResolution, false });
            }
        }

        if (m_currentAmortizeIndex >= m_desiredList.size())
            m_needsGridRebuild = false;

        if (!chunksToLoadThisFrame.empty()) {
            std::sort(chunksToLoadThisFrame.begin(), chunksToLoadThisFrame.end(),
                [](const ChunkSortItem& a, const ChunkSortItem& b) { return a.distanceSq < b.distanceSq; });

            auto dispatchChunk = [&](const ChunkSortItem& item) {
                int64_t itemKey = item.coord.Key();
                if (loadingChunks.find(itemKey) != loadingChunks.end()) {
                    for (auto& activeJob : asyncResults) {
                        if (activeJob.key == itemKey) {
                            activeJob.cancelToken->store(true, std::memory_order_relaxed);
                        }
                    }
                }

                loadingChunks[itemKey] = item.desiredLod;
                int cx = item.coord.cx; int cz = item.coord.cz; int res = item.targetResolution;
                float size = chunkSize;
                int64_t k = itemKey;
                int lod = item.desiredLod;

                auto cancelToken = std::make_shared<std::atomic<bool>>(false);

                auto future = threadPool.Enqueue([cx, cz, res, size, k, lod, cancelToken, this]() {
                    ChunkJobResult jobData;
                    jobData.coord = ChunkCoord{ cx, cz };
                    jobData.lod = lod;

                    // Acquire pooled buffers (reuses heap capacity)
                    PooledMeshBuffers buffers = m_bufferPool.Acquire();
                    jobData.vertices = std::move(buffers.vertices);
                    jobData.indices = std::move(buffers.indices);

                    GenerateChunk(cx, cz, res, size, jobData, cancelToken);

                    return jobData;
                    });

                asyncResults.push_back({ std::move(future), cancelToken, itemKey });
                };

            constexpr size_t MAX_CHUNK_DISPATCHES_PER_FRAME = 4;
            constexpr size_t MAX_ACTIVE_CHUNK_JOBS = 8;

            size_t dispatchedThisFrame = 0;
            bool hasDeferredChunks = false;

            for (const auto& item : chunksToLoadThisFrame) {
                if (dispatchedThisFrame >= MAX_CHUNK_DISPATCHES_PER_FRAME ||
                    asyncResults.size() >= MAX_ACTIVE_CHUNK_JOBS) {
                    hasDeferredChunks = true;
                    break;
                }

                dispatchChunk(item);
                ++dispatchedThisFrame;
            }

            if (hasDeferredChunks) {
                m_needsGridRebuild = true;
                m_currentAmortizeIndex = 0;
            }
        }
    }

    return false;
}

TerrainData Chunk::CalculateHeightAndColor(float worldX, float worldZ) {
    static thread_local FastNoiseLite continentNoise;
    static thread_local FastNoiseLite hillNoise;
    static thread_local FastNoiseLite mountainNoise;
    static thread_local FastNoiseLite ridgeNoise;
    static thread_local FastNoiseLite valleyNoise;
    static thread_local FastNoiseLite peakNoise;
    static thread_local FastNoiseLite detailNoise;
    static thread_local FastNoiseLite plateauNoise;
    static thread_local FastNoiseLite tempNoise;
    static thread_local FastNoiseLite moistNoise;
    static thread_local FastNoiseLite dirtNoise; // --- NEW: Dirt patch noise ---
    static thread_local bool initialized = false;

    if (!initialized) {
        // Huge continental layout.
        continentNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        continentNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        continentNoise.SetFractalOctaves(4);
        continentNoise.SetFractalLacunarity(2.0f);
        continentNoise.SetFractalGain(0.50f);
        continentNoise.SetFrequency(0.00055f);

        // Broad rolling terrain.
        hillNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        hillNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        hillNoise.SetFractalOctaves(4);
        hillNoise.SetFractalLacunarity(2.0f);
        hillNoise.SetFractalGain(0.48f);
        hillNoise.SetFrequency(0.0022f);

        // Mountain belt frequency: a range should span many chunks.
        mountainNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        mountainNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        mountainNoise.SetFractalOctaves(3);
        mountainNoise.SetFractalLacunarity(2.0f);
        mountainNoise.SetFractalGain(0.52f);
        mountainNoise.SetFrequency(0.00075f);

        // Ridges are secondary detail, not the mountain silhouette.
        ridgeNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        ridgeNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        ridgeNoise.SetFractalOctaves(4);
        ridgeNoise.SetFractalLacunarity(2.0f);
        ridgeNoise.SetFractalGain(0.50f);
        ridgeNoise.SetFrequency(0.0025f);

        // Broad internal valleys.
        valleyNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        valleyNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        valleyNoise.SetFractalOctaves(3);
        valleyNoise.SetFractalLacunarity(2.0f);
        valleyNoise.SetFractalGain(0.50f);
        valleyNoise.SetFrequency(0.0011f);

        // Sparse landmark peaks.
        peakNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        peakNoise.SetFrequency(0.00075f);

        // Very small breakup.
        detailNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        detailNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        detailNoise.SetFractalOctaves(3);
        detailNoise.SetFractalLacunarity(2.0f);
        detailNoise.SetFractalGain(0.5f);
        detailNoise.SetFrequency(0.012f);

        // Broad, low-frequency terrain variation used mainly outside mountains.
        plateauNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        plateauNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        plateauNoise.SetFractalOctaves(3);
        plateauNoise.SetFractalLacunarity(2.0f);
        plateauNoise.SetFractalGain(0.52f);
        plateauNoise.SetFrequency(0.0016f);

        tempNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        tempNoise.SetFrequency(0.00028f);

        moistNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        moistNoise.SetFrequency(0.00036f);

        // --- NEW: Dirt Patch Generator ---
        dirtNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        dirtNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        dirtNoise.SetFractalOctaves(3);
        dirtNoise.SetFrequency(0.015f); // Frequency dictates patch size (0.015 = ~60 units wide)

        initialized = true;
    }

    const int seed = s_globalSeed;
    continentNoise.SetSeed(seed + 11);
    hillNoise.SetSeed(seed + 23);
    mountainNoise.SetSeed(seed + 37);
    ridgeNoise.SetSeed(seed + 47);
    valleyNoise.SetSeed(seed + 61);
    peakNoise.SetSeed(seed + 73);
    detailNoise.SetSeed(seed + 89);
    plateauNoise.SetSeed(seed + 97);
    tempNoise.SetSeed(seed + 101);
    moistNoise.SetSeed(seed + 202);
    dirtNoise.SetSeed(seed + 303);

    auto clamp01 = [](float v) {
        return std::clamp(v, 0.0f, 1.0f);
        };

    auto smooth = [](float edge0, float edge1, float x) {
        float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
        };

    auto n01 = [](float n) {
        return (n + 1.0f) * 0.5f;
        };

    // ------------------------------------------------------------------------
    // 1. CONTINENTS / LOWLANDS
    // ------------------------------------------------------------------------
    float continent = n01(continentNoise.GetNoise(worldX, worldZ));
    float landMask = smooth(0.34f, 0.58f, continent);
    float basinMask = 1.0f - smooth(0.22f, 0.43f, continent);

    float broadHills = n01(hillNoise.GetNoise(worldX, worldZ));
    broadHills = (broadHills - 0.5f) * 2.0f;

    float plainUndulation = n01(hillNoise.GetNoise(worldX * 0.48f + 173.0f,
        worldZ * 0.48f - 91.0f));
    plainUndulation = (plainUndulation - 0.5f) * 2.0f;

    float height = 3.0f;
    height += basinMask * broadHills * 4.5f;
    height += landMask * (9.0f + broadHills * 11.0f);
    height += landMask * (1.8f * plainUndulation) * (1.0f - basinMask * 0.45f);

    // ------------------------------------------------------------------------
    // 2. DIRECTIONAL MOUNTAIN BELTS
    // ------------------------------------------------------------------------
    constexpr float c1 = 0.70710678f;
    constexpr float s1 = 0.70710678f;
    constexpr float c2 = 0.86602540f;
    constexpr float s2 = 0.50000000f;

    float x1 = worldX * c1 + worldZ * s1;
    float z1 = -worldX * s1 + worldZ * c1;

    float x2 = worldX * c2 - worldZ * s2;
    float z2 = worldX * s2 + worldZ * c2;

    float rangeA = n01(mountainNoise.GetNoise(x1, z1 * 0.38f));
    float rangeB = n01(mountainNoise.GetNoise(x2, z2 * 0.44f));

    float beltA = smooth(0.50f, 0.68f, rangeA);
    float beltB = smooth(0.56f, 0.74f, rangeB) * 0.72f;

    float mountainMask = clamp01(std::max(beltA, beltB));
    mountainMask *= landMask;

    float foothillA = smooth(0.40f, 0.60f, rangeA);
    float foothillB = smooth(0.46f, 0.64f, rangeB) * 0.70f;
    float foothillMask = clamp01(std::max(foothillA, foothillB));
    foothillMask = foothillMask * landMask * (1.0f - mountainMask * 0.88f);

    // ------------------------------------------------------------------------
    // 3. MASSIF — THE MAIN MOUNTAIN SHAPE
    // ------------------------------------------------------------------------
    float massif = n01(hillNoise.GetNoise(worldX * 0.60f, worldZ * 0.60f));
    massif = smooth(0.30f, 0.76f, massif);

    float mountainHeight = 46.0f + massif * 64.0f;

    height += foothillMask * (14.0f + massif * 28.0f);
    height += mountainMask * mountainHeight;

    // ------------------------------------------------------------------------
    // 4. BROAD VALLEYS THROUGH THE RANGE
    // ------------------------------------------------------------------------
    float valleyA = n01(valleyNoise.GetNoise(x1 * 0.90f, z1 * 0.72f));
    float valleyB = n01(valleyNoise.GetNoise(x2 * 0.88f, z2 * 0.76f));

    float valleyAAmount = smooth(0.18f, 0.42f, 1.0f - valleyA);
    float valleyBAmount = smooth(0.20f, 0.44f, 1.0f - valleyB);
    float valleyMask = std::max(valleyAAmount, valleyBAmount);

    height -= mountainMask * valleyMask * (8.0f + massif * 14.0f);

    // ------------------------------------------------------------------------
    // 5. ROUNDED RIDGES
    // ------------------------------------------------------------------------
    float ridgeA = 1.0f - std::abs(ridgeNoise.GetNoise(x1, z1 * 0.70f));
    float ridgeB = 1.0f - std::abs(ridgeNoise.GetNoise(x2, z2 * 0.78f));

    ridgeA = std::pow(clamp01(ridgeA), 2.4f);
    ridgeB = std::pow(clamp01(ridgeB), 2.4f);

    float ridge = std::max(ridgeA, ridgeB);

    ridge *= (1.0f - valleyMask * 0.65f);
    ridge *= (0.55f + massif * 0.45f);

    height += mountainMask * ridge * 20.0f;

    // ------------------------------------------------------------------------
    // 6. A FEW BIG PEAKS
    // ------------------------------------------------------------------------
    float peakSignal = n01(peakNoise.GetNoise(worldX, worldZ));
    float peakMask = smooth(0.76f, 0.88f, peakSignal);
    peakMask *= mountainMask;
    peakMask *= (1.0f - valleyMask);

    height += peakMask * peakMask * 26.0f;

    // ------------------------------------------------------------------------
    // 7. SMALL SURFACE DETAIL
    // ------------------------------------------------------------------------
    float detail = n01(detailNoise.GetNoise(worldX, worldZ));
    detail = (detail - 0.5f) * 2.0f;

    float detailWeight = 0.45f;
    detailWeight += foothillMask * 0.30f;
    detailWeight += mountainMask * 0.85f;
    detailWeight *= (1.0f - valleyMask * 0.30f);
    height += detail * (1.25f * detailWeight);

    float plateauSignal = n01(plateauNoise.GetNoise(worldX, worldZ));
    float plateauMask = smooth(0.67f, 0.82f, plateauSignal);
    plateauMask *= landMask;
    plateauMask *= (1.0f - mountainMask * 0.92f);
    float plateauBase = std::floor(height / 12.0f + 0.5f) * 12.0f;
    height = std::lerp(height, plateauBase + 1.5f, plateauMask * 0.18f);

    float excess = std::max(0.0f, height - 190.0f);
    height -= excess * 0.45f;
    height = std::max(0.0f, height);

    // ------------------------------------------------------------------------
    // 8. CLIMATE & TEXTURE WEIGHTS
    // ------------------------------------------------------------------------
    float t = clamp01(n01(tempNoise.GetNoise(worldX, worldZ)));
    float m = clamp01(n01(moistNoise.GetNoise(worldX, worldZ)));
    const float BLEND_RANGE = 0.035f;

    const BiomeDefinition& b0 = GetBiomeDefinition(DetermineBiome(clamp01(t - BLEND_RANGE), clamp01(m - BLEND_RANGE)));
    const BiomeDefinition& b1 = GetBiomeDefinition(DetermineBiome(clamp01(t + BLEND_RANGE), clamp01(m - BLEND_RANGE)));
    const BiomeDefinition& b2 = GetBiomeDefinition(DetermineBiome(clamp01(t - BLEND_RANGE), clamp01(m + BLEND_RANGE)));
    const BiomeDefinition& b3 = GetBiomeDefinition(DetermineBiome(clamp01(t + BLEND_RANGE), clamp01(m + BLEND_RANGE)));

    glm::vec3 blendedWeights =
        (b0.textureWeights + b1.textureWeights +
            b2.textureWeights + b3.textureWeights) * 0.25f;

    glm::vec3 blendedColor =
        (b0.groundColor + b1.groundColor +
            b2.groundColor + b3.groundColor) * 0.25f;

    if (blendedWeights.y > 0.4f) {
        float dNoise = n01(dirtNoise.GetNoise(worldX, worldZ)); // 0 to 1

        // Convert 35% of grassy areas into dirt patches
        if (dNoise < 0.35f) {
            // Smoothly blend the edges of the dirt patch
            float blend = smooth(0.20f, 0.35f, dNoise);

            float grassAmount = blendedWeights.y;
            blendedWeights.y = std::lerp(0.0f, grassAmount, blend);

            // Transfer the removed grass weight into the sand/dirt channel (x)
            blendedWeights.z += grassAmount * (1.0f - blend);
        }
    }

    return { height, blendedWeights, blendedColor };
}
float Chunk::GetHeight(float worldX, float worldZ) {
    return CalculateHeightAndColor(worldX, worldZ).height;
}
float Chunk::GetCachedHeightFromGrid(float worldX, float worldZ) {
    const float chunkSize = m_chunkSize;
    int chunkX = static_cast<int>(std::floor(worldX / chunkSize));
    int chunkZ = static_cast<int>(std::floor(worldZ / chunkSize));
    int64_t chunkKey = ChunkCoord{ chunkX, chunkZ }.Key();

    std::shared_lock<std::shared_mutex> lock(s_activeChunkGridsMutex);

    auto it = s_activeChunkGrids.find(chunkKey);
    if (it == s_activeChunkGrids.end() ||
        it->second.resolution < 2 ||
        it->second.heightData.empty()) {
        lock.unlock();
        return CalculateHeightAndColor(worldX, worldZ).height;
    }

    const ChunkGridCache& grid = it->second;
    const int res = grid.resolution;

    float localX = (worldX - chunkX * chunkSize) / chunkSize;
    float localZ = (worldZ - chunkZ * chunkSize) / chunkSize;

    float gridX = localX * static_cast<float>(res - 1);
    float gridZ = localZ * static_cast<float>(res - 1);

    int x0 = std::clamp(static_cast<int>(std::floor(gridX)), 0, res - 2);
    int z0 = std::clamp(static_cast<int>(std::floor(gridZ)), 0, res - 2);
    int x1 = x0 + 1;
    int z1 = z0 + 1;

    float tx = std::clamp(gridX - static_cast<float>(x0), 0.0f, 1.0f);
    float tz = std::clamp(gridZ - static_cast<float>(z0), 0.0f, 1.0f);

    const auto& heights = grid.heightData;
    float h00 = heights[z0 * res + x0];
    float h10 = heights[z0 * res + x1];
    float h01 = heights[z1 * res + x0];
    float h11 = heights[z1 * res + x1];

    float h0 = std::lerp(h00, h10, tx);
    float h1 = std::lerp(h01, h11, tx);
    return std::lerp(h0, h1, tz);
}
void Chunk::PublishGridCache(int64_t chunkKey, ChunkGridCache&& gridCache) {
    std::unique_lock<std::shared_mutex> lock(s_activeChunkGridsMutex);
    s_activeChunkGrids[chunkKey] = std::move(gridCache);
}
void Chunk::RemoveGridCache(int64_t chunkKey) {
    std::unique_lock<std::shared_mutex> lock(s_activeChunkGridsMutex);
    s_activeChunkGrids.erase(chunkKey);
}

void Chunk::GenerateChunk(int chunkX, int chunkZ, int resolution, float chunkSize,
    ChunkJobResult& outResult,
    std::shared_ptr<std::atomic<bool>> cancelToken)
{
    auto isCancelled = [&]() {
        return cancelToken && cancelToken->load(std::memory_order_relaxed);
        };

    auto discardCancelledResult = [&]() {
        outResult.vertices.clear();
        outResult.indices.clear();
        outResult.grassInstances.clear();
        outResult.props.clear();
        outResult.boids.clear();
        outResult.physicsGrid = {};
        };

    outResult.vertices.clear();
    outResult.indices.clear();
    outResult.grassInstances.clear();
    outResult.props.clear();
    outResult.boids.clear();

    outResult.vertices.reserve(resolution * resolution + (resolution - 1) * 8);
    outResult.indices.reserve(((resolution - 1) * (resolution - 1) * 6) + ((resolution - 1) * 4 * 6));

    if (isCancelled()) {
        discardCancelledResult();
        return;
    }

    const float step = chunkSize / (float)(resolution - 1);
    float originX = chunkX * chunkSize;
    float originZ = chunkZ * chunkSize;

    int pad = 2;
    int gridSize = resolution + pad * 2;
    std::vector<float> heightGrid(gridSize * gridSize);
    std::vector<glm::vec3> colorGrid(gridSize * gridSize);
    std::vector<glm::vec3> groundColorGrid(gridSize * gridSize);

    for (int gz = 0; gz < gridSize; ++gz) {
        if (isCancelled()) {
            discardCancelledResult();
            return;
        }

        const float worldZ = originZ + (gz - pad) * step;
        const int rowOffset = gz * gridSize;

        for (int gx = 0; gx < gridSize; ++gx) {
            const float worldX = originX + (gx - pad) * step;
            TerrainData data = CalculateHeightAndColor(worldX, worldZ);

            heightGrid[rowOffset + gx] = data.height;
            colorGrid[rowOffset + gx] = data.biomeWeights;
            groundColorGrid[rowOffset + gx] = data.groundColor;
        }
    }

    std::vector<float> physicsGrid(resolution * resolution);
    for (int z = 0; z < resolution; ++z) {
        for (int x = 0; x < resolution; ++x) {
            physicsGrid[z * resolution + x] = heightGrid[(z + pad) * gridSize + (x + pad)];
        }
    }

    outResult.physicsGrid = ChunkGridCache{ resolution, std::move(physicsGrid) };

    auto GetCachedHeight = [&](int localX, int localZ) -> float {
        int cx = std::clamp(localX + pad, 0, gridSize - 1);
        int cz = std::clamp(localZ + pad, 0, gridSize - 1);
        return heightGrid[cz * gridSize + cx];
        };
    auto GetCachedColor = [&](int localX, int localZ) -> glm::vec3 {
        int cx = std::clamp(localX + pad, 0, gridSize - 1);
        int cz = std::clamp(localZ + pad, 0, gridSize - 1);
        return colorGrid[cz * gridSize + cx];
        };
    auto GetCachedGroundColor = [&](int localX, int localZ) -> glm::vec3 {
        int cx = std::clamp(localX + pad, 0, gridSize - 1);
        int cz = std::clamp(localZ + pad, 0, gridSize - 1);
        return groundColorGrid[cz * gridSize + cx];
        };

    for (int z = 0; z < resolution; z++) {
        if (isCancelled()) {
            discardCancelledResult();
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

            ModelVertex v{};
            v.pos = glm::vec3(worldX, h, worldZ);
            v.texCoord = glm::vec2(worldX * 0.02f, worldZ * 0.02f);

            // Assign the base ground color
            v.color = GetCachedGroundColor(x, z);

            // Pack the biome weights (dirt, grass, rock) into the tangent for the fragment shader
            glm::vec3 biomeWeights = GetCachedColor(x, z);

            glm::vec3 tangentX(2.0f * step, hR - hL, 0.0f);
            glm::vec3 tangentZ(0.0f, hU - hD, 2.0f * step);
            v.normal = glm::normalize(glm::cross(tangentZ, tangentX));
            glm::vec3 t = glm::normalize(tangentX);
            t = glm::normalize(t - v.normal * glm::dot(v.normal, t));

            // Note: v.tangent.w can store a 4th texture weight if needed in the future
            v.tangent = glm::vec4(biomeWeights, 1.0f);

            int cx = ((x + 1) / 2) * 2;
            int cz = ((z + 1) / 2) * 2;

            float hCoarse = GetCachedHeight(cx, cz);
            float hL_coarse = GetCachedHeight(cx - 2, cz);
            float hR_coarse = GetCachedHeight(cx + 2, cz);
            float hD_coarse = GetCachedHeight(cx, cz - 2);
            float hU_coarse = GetCachedHeight(cx, cz + 2);

            glm::vec3 coarseTangentX(4.0f * step, hR_coarse - hL_coarse, 0.0f);
            glm::vec3 coarseTangentZ(0.0f, hU_coarse - hD_coarse, 4.0f * step);
            glm::vec3 coarseNormal = glm::normalize(glm::cross(coarseTangentZ, coarseTangentX));

            v.coarsePos = glm::vec3(originX + cx * step, hCoarse, originZ + cz * step);
            v.coarseNormal = coarseNormal;
            outResult.vertices.push_back(v);
        }
    }

    for (int z = 0; z < resolution - 1; z++) {
        for (int x = 0; x < resolution - 1; x++) {
            uint32_t i0 = z * resolution + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = (z + 1) * resolution + x;
            uint32_t i3 = i2 + 1;
            outResult.indices.insert(outResult.indices.end(), { i0, i1, i2, i1, i3, i2 });
        }
    }

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

    if (isCancelled()) {
        discardCancelledResult();
        return;
    }

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

    if (isCancelled()) {
        discardCancelledResult();
        return;
    }

    auto GetFastLocalData = [&](float wX, float wZ) -> TerrainData {
        float localX = wX - originX;
        float localZ = wZ - originZ;

        if (localX < 0.0f || localX >= m_chunkSize || localZ < 0.0f || localZ >= m_chunkSize) {
            return CalculateHeightAndColor(wX, wZ);
        }

        float gridX = (localX / m_chunkSize) * (resolution - 1);
        float gridZ = (localZ / m_chunkSize) * (resolution - 1);

        int x0 = std::clamp(static_cast<int>(gridX), 0, resolution - 2);
        int z0 = std::clamp(static_cast<int>(gridZ), 0, resolution - 2);
        int x1 = x0 + 1;
        int z1 = z0 + 1;

        float tx = gridX - x0;
        float tz = gridZ - z0;

        float h00 = GetCachedHeight(x0, z0);
        float h10 = GetCachedHeight(x1, z0);
        float h01 = GetCachedHeight(x0, z1);
        float h11 = GetCachedHeight(x1, z1);

        glm::vec3 w00 = GetCachedColor(x0, z0);
        glm::vec3 w10 = GetCachedColor(x1, z0);
        glm::vec3 w01 = GetCachedColor(x0, z1);
        glm::vec3 w11 = GetCachedColor(x1, z1);

        glm::vec3 c00 = GetCachedGroundColor(x0, z0);
        glm::vec3 c10 = GetCachedGroundColor(x1, z0);
        glm::vec3 c01 = GetCachedGroundColor(x0, z1);
        glm::vec3 c11 = GetCachedGroundColor(x1, z1);

        float h0 = std::lerp(h00, h10, tx);
        float h1 = std::lerp(h01, h11, tx);
        float finalH = std::lerp(h0, h1, tz);

        glm::vec3 w0 = glm::mix(w00, w10, tx);
        glm::vec3 w1 = glm::mix(w01, w11, tx);
        glm::vec3 finalW = glm::mix(w0, w1, tz);

        glm::vec3 c0 = glm::mix(c00, c10, tx);
        glm::vec3 c1 = glm::mix(c01, c11, tx);
        glm::vec3 finalC = glm::mix(c0, c1, tz);

        return { finalH, finalW, finalC };
        };

    GenerateChunkProps(chunkX, chunkZ, outResult.lod, outResult, GetFastLocalData);
    GenerateChunkSwarms(chunkX, chunkZ, outResult.lod, outResult, GetFastLocalData);

    if (outResult.lod == 0) {
        const float GRASS_STEP = 8.0f;
        int bladesPerCell = 18;
        const float JITTER_RADIUS = 4.0f;

        int gridCells = static_cast<int>(chunkSize / GRASS_STEP);
        outResult.grassInstances.reserve(gridCells * gridCells * bladesPerCell);

        for (float localX = 0.0f; localX < chunkSize; localX += GRASS_STEP) {
            if (isCancelled()) {
                discardCancelledResult();
                return;
            }
            for (float localZ = 0.0f; localZ < chunkSize; localZ += GRASS_STEP) {

                float baseX = originX + localX;
                float baseZ = originZ + localZ;

                TerrainData data = GetFastLocalData(baseX, baseZ);

                if (data.height <= -5.0f || data.biomeWeights.y < 0.2f) continue;

                float hR = GetFastLocalData(baseX + 2.0f, baseZ).height;
                float hU = GetFastLocalData(baseX, baseZ + 2.0f).height;
                glm::vec3 normal = glm::normalize(glm::vec3(data.height - hR, 2.0f, data.height - hU));
                if (normal.y < 0.75f) continue;

                float densityMask = glm::smoothstep(0.35f, 0.85f, data.biomeWeights.y);
                uint32_t cellHash = Hash2D(static_cast<int>(baseX * 10), static_cast<int>(baseZ * 10), s_globalSeed);

                auto fastRand = [](uint32_t& state) -> float {
                    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                    return (float)state / (float)UINT32_MAX;
                    };

                for (int n = 0; n < bladesPerCell; n++) {
                    uint32_t bladeSeed = cellHash + n;
                    if (fastRand(bladeSeed) > densityMask) continue;

                    float r = sqrt(fastRand(bladeSeed)) * JITTER_RADIUS;
                    float theta = fastRand(bladeSeed) * 2.0f * 3.14159f;
                    float bladeX = baseX + r * cos(theta);
                    float bladeZ = baseZ + r * sin(theta);

                    float bladeY = GetFastLocalData(bladeX, bladeZ).height;

                    GrassInstance inst{};
                    inst.position = glm::vec3(bladeX, bladeY, bladeZ);
                    inst.rotation = fastRand(bladeSeed) * 2.0f * 3.14159f;

                    float height = 1.4f + (fastRand(bladeSeed) * 1.8f);
                    height *= std::lerp(0.4f, 1.0f, densityMask);

                    float baseWidth = 0.65f + (fastRand(bladeSeed) * 0.45f);
                    inst.scale = glm::vec3(baseWidth, height, baseWidth);
                    inst.windOffset = fastRand(bladeSeed) * 50.0f;

                    outResult.grassInstances.push_back(inst);
                }
            }
        }
    }
}

void Chunk::GenerateChunkProps(int chunkX, int chunkZ, int lod, ChunkJobResult& outResult,
    const std::function<TerrainData(float, float)>& heightColorFunc) {

    const float stepSize = 20.0f;
    int steps = static_cast<int>(m_chunkSize / stepSize);

    static thread_local FastNoiseLite treeNoise;
    static thread_local FastNoiseLite stoneNoise;
    static thread_local bool noiseInit = false;

    if (!noiseInit) {
        treeNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        treeNoise.SetFrequency(0.003f);
        stoneNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        stoneNoise.SetFrequency(0.08f);
        noiseInit = true;
    }

    treeNoise.SetSeed(s_globalSeed + 420);
    stoneNoise.SetSeed(s_globalSeed + 840);

    for (int ix = 0; ix < steps; ++ix) {
        for (int iz = 0; iz < steps; ++iz) {
            float worldX = chunkX * m_chunkSize + (ix * stepSize);
            float worldZ = chunkZ * m_chunkSize + (iz * stepSize);

            BiomeType biome = GetDominantBiome(worldX, worldZ);
            const BiomeDefinition& def = GetBiomeDefinition(biome);
            if (def.props.empty()) continue;

            uint32_t coordHash = Hash2D(chunkX * 1000 + ix, chunkZ * 1000 + iz, s_globalSeed);
            TerrainData data = heightColorFunc(worldX, worldZ);

            float masks[2] = { treeNoise.GetNoise(worldX, worldZ), stoneNoise.GetNoise(worldX, worldZ) };
            float spawnChance = (coordHash % 1000) / 1000.0f;

            for (const auto& rule : def.props) {
                if (lod > rule.maxLod) continue;
                if (data.height < rule.minHeight || data.height > rule.maxHeight) continue;
                if (masks[rule.noiseIndex] < rule.noiseThreshold) continue;

                if (spawnChance < rule.spawnChance) {
                    PropInstance prop{};
                    prop.position = glm::vec3(worldX, data.height + rule.groundOffset, worldZ);

                    prop.rotation = glm::vec3(
                        rule.alignToNormal ? static_cast<float>(coordHash % 360) : 0.0f,
                        static_cast<float>((coordHash >> 4) % 360),
                        rule.alignToNormal ? static_cast<float>((coordHash >> 8) % 360) : 0.0f
                    );

                    float scaleT = static_cast<float>((coordHash >> 8) % 100) / 100.0f;
                    prop.scale = glm::vec3(std::lerp(rule.minScale, rule.maxScale, scaleT));
                    prop.customPayload = static_cast<float>(coordHash % 100) / 100.0f;
                    prop.lodMeshes = rule.lodMeshes;

                    outResult.props.push_back(prop);
                    break;
                }
            }
        }
    }
}

void Chunk::GenerateChunkSwarms(int chunkX, int chunkZ, int lod, ChunkJobResult& outResult,
    const std::function<TerrainData(float, float)>& heightColorFunc) {

    if (lod >= 2) return;

    float centerWorldX = chunkX * m_chunkSize + (m_chunkSize * 0.5f);
    float centerWorldZ = chunkZ * m_chunkSize + (m_chunkSize * 0.5f);

    BiomeType biome = GetDominantBiome(centerWorldX, centerWorldZ);
    const BiomeDefinition& def = GetBiomeDefinition(biome);
    if (def.swarms.empty()) return;

    TerrainData data = heightColorFunc(centerWorldX, centerWorldZ);
    uint32_t coordHash = Hash2D(chunkX, chunkZ, s_globalSeed);
    float spawnChance = (coordHash % 1000) / 1000.0f;

    for (const auto& rule : def.swarms) {
        if (data.height < rule.minHeight || data.height > rule.maxHeight) continue;

        if (spawnChance < rule.spawnChance) {
            outResult.boids.reserve(outResult.boids.size() + rule.boidCount);
            for (int i = 0; i < rule.boidCount; i++) {
                BoidInstance b{};
                float jitterX = ((coordHash * (i + 1) % 100) / 100.0f) * (rule.spreadRadius * 2) - rule.spreadRadius;
                float jitterZ = ((coordHash * (i + 3) % 100) / 100.0f) * (rule.spreadRadius * 2) - rule.spreadRadius;

                float scaleT = ((coordHash * (i + 7) % 100) / 100.0f);
                float randomScale = std::lerp(rule.minScale, rule.maxScale, scaleT);

                b.position = glm::vec4(centerWorldX + jitterX, data.height + rule.verticalOffset + (i % 4), centerWorldZ + jitterZ, randomScale);

                float randomTimeOffset = static_cast<float>((coordHash * i) % 1000);
                b.velocity = glm::vec4(1.0f, 0.0f, 0.0f, randomTimeOffset);

                outResult.boids.push_back(b);
            }
            break;
        }
    }
}

void Chunk::UpdateFogParamsBasedOnData(VulkanRenderer& renderer) {
    float totalViewDistance = viewDistanceChunks * chunkSize;
    float fogStart = 0.3125f * totalViewDistance;
    float fogEnd = 0.3320f * totalViewDistance;
    renderer.SetFogParams(fogStart, fogEnd);
}
BiomeType Chunk::GetDominantBiome(float worldX, float worldZ) {
    static thread_local FastNoiseLite tempNoise;
    static thread_local FastNoiseLite moistNoise;
    static thread_local bool init = false;

    if (!init) {
        tempNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        tempNoise.SetFrequency(0.00028f);
        moistNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        moistNoise.SetFrequency(0.00036f);
        init = true;
    }
    tempNoise.SetSeed(s_globalSeed + 101);
    moistNoise.SetSeed(s_globalSeed + 202);

    float t = std::clamp((tempNoise.GetNoise(worldX, worldZ) + 1.0f) * 0.5f, 0.0f, 1.0f);
    float m = std::clamp((moistNoise.GetNoise(worldX, worldZ) + 1.0f) * 0.5f, 0.0f, 1.0f);
    return DetermineBiome(t, m);
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

static float GetClosestChunkDistance(const glm::vec3& cameraPos, const glm::vec3& chunkCenter, float chunkSize) {
    float distToCenter = glm::distance(cameraPos, chunkCenter);
    // 0.75f is roughly half the diagonal of a square (0.707), with a tiny bit of extra safety padding
    float chunkRadius = chunkSize * 0.75f;

    // Never return a negative distance if we are standing inside the chunk
    return std::max(0.0f, distToCenter - chunkRadius);
}