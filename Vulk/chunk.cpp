#include "chunk.h"
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <iostream>

// --- Static seed for height queries ---
int Chunk::s_globalSeed = 23645;
float Chunk::m_chunkSize = 512.0f;
std::unordered_map<int64_t, ChunkGridCache> Chunk::s_activeChunkGrids;

Chunk::Chunk() : threadPool(std::max(1u, std::thread::hardware_concurrency() - 1)) {}

void Chunk::Init(VulkanRenderer& renderer) {
    UpdateFogParamsBasedOnData(renderer);
    renderer.SetTerrainChunkSize(chunkSize);

    m_chunkSize = chunkSize;
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
    if (closestEdgeDist > g_Settings.staticFadeEnd) return 4;

    // LOD 3: Terrain + Billboard Trees (Past tree fade start)
    if (closestEdgeDist > g_Settings.staticFadeStart) return 3;

    // LOD 2: Terrain + Low Poly Trees, NO Grass (Past grass fade end)
    if (closestEdgeDist > g_Settings.grassFadeEnd) return 2;

    // LOD 1: Terrain + Med Trees + Thin Grass (Past grass fade start)
    if (closestEdgeDist > g_Settings.grassFadeStart) return 1;

    // LOD 0: High Poly Everything
    return 0;
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

            for (const auto& treeData : result.trees) {
                Entity treeEntity = scene.GetRegistry().CreateEntity();

                uint32_t coordHash = Hash2D(static_cast<int>(treeData.position.x), static_cast<int>(treeData.position.z), s_globalSeed);

                TransformComponent tComp;
                tComp.position = treeData.position;
                tComp.rotation = treeData.rotation;
                tComp.scale = treeData.scale;

                tComp.isDirty = true;
                scene.GetRegistry().AddComponent<TransformComponent>(treeEntity, tComp);

                RenderComponent rComp;
                if (result.lod == 0) {
                    rComp.meshName = "assets/models/tree/tree.obj"; // Full 1k vert mesh
                }
                else if (result.lod == 1) {
                    rComp.meshName = "assets/models/tree/tree_lod1.obj"; // Decimated mesh (~300 verts)
                }
                else {
                    rComp.meshName = "assets/models/tree/tree_billboard.obj"; // 2-triangle cross plane
                    tComp.scale = treeData.scale * glm::vec3(45.0f, 45.0f, 45.0f) + glm::vec3(10);
                }

                rComp.type = MeshType::Static;
                rComp.isInstanced = true;
                rComp.isVisible = true;
                scene.GetRegistry().AddComponent<RenderComponent>(treeEntity, rComp);

                ChunkPropComponent cComp;
                cComp.chunkKey = resultKey;
                scene.GetRegistry().AddComponent<ChunkPropComponent>(treeEntity, cComp);
            }

            scene.MarkDirty();

            renderer.AddTerrainChunk(resultKey, result.coord.cx, result.coord.cz, result.lod, result.vertices, result.indices);
            renderer.AddGrass(resultKey, result.grassInstances);

            if (!result.butterflies.empty()) {
                renderer.AddBoid(resultKey, result.butterflies, 4);
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

        for (int dz = -viewDistanceChunks; dz <= viewDistanceChunks; ++dz) {
            for (int dx = -viewDistanceChunks; dx <= viewDistanceChunks; ++dx) {
                ChunkCoord coord{ camChunkX + dx, camChunkZ + dz };
                int64_t k = coord.Key();
                m_desiredKeys.push_back(k);
                m_desiredKeysLookup.insert(k);
                m_desiredList.emplace_back(k, std::make_pair(coord.cx, coord.cz));
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

    return false;
}

std::pair<float, glm::vec3> Chunk::CalculateHeightAndColor(float worldX, float worldZ) {
    // ========================================================================
    // EXPERIMENT V4 - BROAD GEOLOGY + DIRECTIONAL MOUNTAIN CHAINS
    //
    // V2 gave us the right general mountain strength. V3 accidentally
    // multiplied the FastNoise frequency twice when stretching the rotated
    // coordinates, effectively making the mountain signal almost constant.
    // V4 keeps V2's reliable height ranges and adds directionality correctly.
    // ========================================================================

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

    // Keep the V5 plains more alive: broad hills, secondary undulation, and
    // occasional tableland character. None of these signals participate in
    // the mountain mask, so the V4 mountain continuity remains unchanged.
    float plainUndulation = n01(hillNoise.GetNoise(worldX * 0.48f + 173.0f,
        worldZ * 0.48f - 91.0f));
    plainUndulation = (plainUndulation - 0.5f) * 2.0f;

    float height = 3.0f;
    height += basinMask * broadHills * 4.5f;
    height += landMask * (9.0f + broadHills * 11.0f);
    height += landMask * (1.8f * plainUndulation) * (1.0f - basinMask * 0.45f);

    // ------------------------------------------------------------------------
    // 2. DIRECTIONAL MOUNTAIN BELTS
    //
    // Rotate the coordinates, then scale ONE axis.  FastNoise already applies
    // the configured frequency internally, so we must NOT multiply both axes
    // by another tiny "frequency" here.  The anisotropic scale only controls
    // shape direction.
    // ------------------------------------------------------------------------
    constexpr float c1 = 0.70710678f;
    constexpr float s1 = 0.70710678f;
    constexpr float c2 = 0.86602540f;
    constexpr float s2 = 0.50000000f;

    float x1 = worldX * c1 + worldZ * s1;
    float z1 = -worldX * s1 + worldZ * c1;

    float x2 = worldX * c2 - worldZ * s2;
    float z2 = worldX * s2 + worldZ * c2;

    // Long-axis stretching: variation is slower along the second coordinate,
    // producing long mountain chains rather than circular islands.
    float rangeA = n01(mountainNoise.GetNoise(x1, z1 * 0.38f));
    float rangeB = n01(mountainNoise.GetNoise(x2, z2 * 0.44f));

    // Keep A as the main chain and B as a weaker crossing system.
    float beltA = smooth(0.50f, 0.68f, rangeA);
    float beltB = smooth(0.56f, 0.74f, rangeB) * 0.72f;

    float mountainMask = clamp01(std::max(beltA, beltB));
    mountainMask *= landMask;

    // Soft outer shoulder around every mountain chain.
    float foothillA = smooth(0.40f, 0.60f, rangeA);
    float foothillB = smooth(0.46f, 0.64f, rangeB) * 0.70f;
    float foothillMask = clamp01(std::max(foothillA, foothillB));
    foothillMask = foothillMask * landMask * (1.0f - mountainMask * 0.88f);

    // ------------------------------------------------------------------------
    // 3. MASSIF — THE MAIN MOUNTAIN SHAPE
    // ------------------------------------------------------------------------
    float massif = n01(hillNoise.GetNoise(worldX * 0.60f, worldZ * 0.60f));
    massif = smooth(0.30f, 0.76f, massif);

    // Large and stable mountain body.
    float mountainHeight = 46.0f + massif * 64.0f;

    // Foothills taper naturally into the plains.
    height += foothillMask * (14.0f + massif * 28.0f);
    height += mountainMask * mountainHeight;

    // ------------------------------------------------------------------------
    // 4. BROAD VALLEYS THROUGH THE RANGE
    // ------------------------------------------------------------------------
    float valleyA = n01(valleyNoise.GetNoise(x1 * 0.90f, z1 * 0.72f));
    float valleyB = n01(valleyNoise.GetNoise(x2 * 0.88f, z2 * 0.76f));

    // Only the lower part of each valley signal carves.  This produces broad
    // passes rather than slicing entire mountains in half.
    float valleyAAmount = smooth(0.18f, 0.42f, 1.0f - valleyA);
    float valleyBAmount = smooth(0.20f, 0.44f, 1.0f - valleyB);
    float valleyMask = std::max(valleyAAmount, valleyBAmount);

    // Carving is deliberately modest. Mountains remain clearly present.
    height -= mountainMask * valleyMask * (8.0f + massif * 14.0f);

    // ------------------------------------------------------------------------
    // 5. ROUNDED RIDGES
    // ------------------------------------------------------------------------
    float ridgeA = 1.0f - std::abs(ridgeNoise.GetNoise(x1, z1 * 0.70f));
    float ridgeB = 1.0f - std::abs(ridgeNoise.GetNoise(x2, z2 * 0.78f));

    ridgeA = std::pow(clamp01(ridgeA), 2.4f);
    ridgeB = std::pow(clamp01(ridgeB), 2.4f);

    float ridge = std::max(ridgeA, ridgeB);

    // Keep valleys visually open and keep ridges subordinate to the massif.
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

    // Sparse, broad peaks — never enough to turn the entire range into spikes.
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

    // V5-style broad tablelands in otherwise calm lowland regions.
    // The blend is intentionally weak so plains gain variety without becoming
    // obviously terraced or disrupting the V4 mountain profile.
    float plateauSignal = n01(plateauNoise.GetNoise(worldX, worldZ));
    float plateauMask = smooth(0.67f, 0.82f, plateauSignal);
    plateauMask *= landMask;
    plateauMask *= (1.0f - mountainMask * 0.92f);
    float plateauBase = std::floor(height / 12.0f + 0.5f) * 12.0f;
    height = std::lerp(height, plateauBase + 1.5f, plateauMask * 0.18f);

    // Final safety compression.  We want rare high peaks, not pathological
    // needle mountains if multiple signals happen to align.
    float excess = std::max(0.0f, height - 190.0f);
    height -= excess * 0.45f;
    height = std::max(0.0f, height);

    // ------------------------------------------------------------------------
    // 8. CLIMATE -> MATERIALS ONLY
    // ------------------------------------------------------------------------
    float t = clamp01(n01(tempNoise.GetNoise(worldX, worldZ)));
    float m = clamp01(n01(moistNoise.GetNoise(worldX, worldZ)));
    const float BLEND_RANGE = 0.035f;

    BiomeProperties properties[4] = {};
    properties[0] = GetBiomeProperties(DetermineBiome(clamp01(t - BLEND_RANGE), clamp01(m - BLEND_RANGE)));
    properties[1] = GetBiomeProperties(DetermineBiome(clamp01(t + BLEND_RANGE), clamp01(m - BLEND_RANGE)));
    properties[2] = GetBiomeProperties(DetermineBiome(clamp01(t - BLEND_RANGE), clamp01(m + BLEND_RANGE)));
    properties[3] = GetBiomeProperties(DetermineBiome(clamp01(t + BLEND_RANGE), clamp01(m + BLEND_RANGE)));

    glm::vec3 blendedWeights =
        (properties[0].textureWeights + properties[1].textureWeights +
            properties[2].textureWeights + properties[3].textureWeights) * 0.25f;

    return { height, blendedWeights };
}
float Chunk::GetHeight(float worldX, float worldZ) {
    return CalculateHeightAndColor(worldX, worldZ).first;
}
float Chunk::GetCachedHeightFromGrid(float worldX, float worldZ) {
    // 1. Convert world coordinates to chunk grid coordinates
    int chunkX = static_cast<int>(std::floor(worldX / m_chunkSize));
    int chunkZ = static_cast<int>(std::floor(worldZ / m_chunkSize));

    int64_t chunkKey = ChunkCoord{ chunkX, chunkZ }.Key();
    auto it = s_activeChunkGrids.find(chunkKey);

    // Fallback: If chunk isn't loaded in memory yet, evaluate raw noise.
    // NOTE: CalculateHeightAndColor MUST be a static method for this line to compile!
    if (it == s_activeChunkGrids.end() || it->second.heightData.empty()) {
        return CalculateHeightAndColor(worldX, worldZ).first;
    }

    // 2. Get normalized local position within the chunk [0.0 to 1.0]
    float localX = (worldX - (chunkX * m_chunkSize)) / m_chunkSize;
    float localZ = (worldZ - (chunkZ * m_chunkSize)) / m_chunkSize;

    // 3. Map to exact array cell indices
    const int res = it->second.resolution;
    float gridX = localX * (res - 1);
    float gridZ = localZ * (res - 1);

    int x0 = std::clamp(static_cast<int>(gridX), 0, res - 2);
    int z0 = std::clamp(static_cast<int>(gridZ), 0, res - 2);
    int x1 = x0 + 1;
    int z1 = z0 + 1;

    // 4. Fetch the 4 corner heights from the cached array
    const auto& heights = it->second.heightData;
    float h00 = heights[z0 * res + x0];
    float h10 = heights[z0 * res + x1];
    float h01 = heights[z1 * res + x0];
    float h11 = heights[z1 * res + x1];

    // 5. Bilinear interpolation for smooth sub-grid height estimation
    float tx = gridX - x0;
    float tz = gridZ - z0;
    float h0 = std::lerp(h00, h10, tx);
    float h1 = std::lerp(h01, h11, tx);

    return std::lerp(h0, h1, tz);
}
void Chunk::RemoveGridCache(int64_t chunkKey) {
    s_activeChunkGrids.erase(chunkKey);
}

void Chunk::GenerateChunk(int chunkX, int chunkZ, int resolution, float chunkSize,
    ChunkJobResult& outResult,
    std::shared_ptr<std::atomic<bool>> cancelToken)
{
    // Clear outputs
    outResult.vertices.clear();
    outResult.indices.clear();
    outResult.grassInstances.clear();
    outResult.trees.clear();

    outResult.vertices.reserve(resolution * resolution + (resolution - 1) * 8);
    outResult.indices.reserve(((resolution - 1) * (resolution - 1) * 6) + ((resolution - 1) * 4 * 6));

    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) return;

    const float step = chunkSize / (float)(resolution - 1);
    float originX = chunkX * chunkSize;
    float originZ = chunkZ * chunkSize;

    int gridSize = resolution + 2;
    std::vector<float> heightGrid(gridSize * gridSize);
    std::vector<glm::vec3> colorGrid(gridSize * gridSize);

    for (int gz = 0; gz < gridSize; ++gz) {
        float worldZ = originZ + (gz - 1) * step;
        int rowOffset = gz * gridSize;
        for (int gx = 0; gx < gridSize; ++gx) {
            float worldX = originX + (gx - 1) * step;
            auto [finalHeight, blendedWeights] = CalculateHeightAndColor(worldX, worldZ);
            heightGrid[rowOffset + gx] = finalHeight;
            colorGrid[rowOffset + gx] = blendedWeights;
        }
    }

    std::vector<float> physicsGrid(resolution * resolution);
    for (int z = 0; z < resolution; ++z) {
        for (int x = 0; x < resolution; ++x) {
            physicsGrid[z * resolution + x] = heightGrid[(z + 1) * gridSize + (x + 1)];
        }
    }

    int64_t chunkKey = ChunkCoord{ chunkX, chunkZ }.Key();
    s_activeChunkGrids[chunkKey] = ChunkGridCache{ resolution, std::move(physicsGrid) };

    auto GetCachedHeight = [&](int localX, int localZ) -> float {
        return heightGrid[(localZ + 1) * gridSize + (localX + 1)];
        };
    auto GetCachedColor = [&](int localX, int localZ) -> glm::vec3 {
        return colorGrid[(localZ + 1) * gridSize + (localX + 1)];
        };

    const float coarseStep = step * 2.0f;
    std::unordered_map<int64_t, float> coarseHeightCache;
    coarseHeightCache.reserve((size_t)(resolution / 2 + 4) * (resolution / 2 + 4));
    auto CoarseKey = [](int ix, int iz) -> int64_t {
        return (static_cast<int64_t>(ix) << 32) | (static_cast<uint32_t>(iz));
        };
    auto GetCoarseHeight = [&](float snappedWorldX, float snappedWorldZ) -> float {
        int ix = (int)std::lround(snappedWorldX / coarseStep);
        int iz = (int)std::lround(snappedWorldZ / coarseStep);
        int64_t key = CoarseKey(ix, iz);
        auto it = coarseHeightCache.find(key);
        if (it != coarseHeightCache.end()) return it->second;
        float h = CalculateHeightAndColor(snappedWorldX, snappedWorldZ).first;
        coarseHeightCache.emplace(key, h);
        return h;
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

            ModelVertex v{};
            v.pos = glm::vec3(worldX, h, worldZ);
            v.texCoord = glm::vec2(worldX * 0.02f, worldZ * 0.02f);
            v.color = GetCachedColor(x, z);

            glm::vec3 tangentX(2.0f * step, hR - hL, 0.0f);
            glm::vec3 tangentZ(0.0f, hU - hD, 2.0f * step);
            v.normal = glm::normalize(glm::cross(tangentZ, tangentX));
            glm::vec3 t = glm::normalize(tangentX);
            t = glm::normalize(t - v.normal * glm::dot(v.normal, t));
            v.tangent = glm::vec4(t, 1.0f);

            float coarseX = std::floor(worldX / coarseStep + 0.5f) * coarseStep;
            float coarseZ = std::floor(worldZ / coarseStep + 0.5f) * coarseStep;
            float hCoarse = GetCoarseHeight(coarseX, coarseZ);
            float hL_coarse = GetCoarseHeight(coarseX - coarseStep, coarseZ);
            float hR_coarse = GetCoarseHeight(coarseX + coarseStep, coarseZ);
            float hD_coarse = GetCoarseHeight(coarseX, coarseZ - coarseStep);
            float hU_coarse = GetCoarseHeight(coarseX, coarseZ + coarseStep);
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

    auto fastRand = [](uint32_t& state) -> float {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float)state / (float)UINT32_MAX;
        };
    uint32_t rngState = static_cast<uint32_t>(chunkX * 73856 + chunkZ * 19349 + 1);

    // ================================================================
    // ---- TREE generation (Deterministic World-Space scatter) ----
    // ================================================================
    GenerateChunkTrees(chunkX, chunkZ, outResult.lod, outResult);

    GenerateChunkSwarms(chunkX, chunkZ, outResult.lod, outResult);

    if (outResult.lod >= 2) {
        return;
    }

    int stride = (outResult.lod == 1) ? 2 : 1;
    int bladesPerVertex = (outResult.lod == 1) ? 10 : 50;
    float widthMultiplier = (outResult.lod == 1) ? 1.5f : 1.0f;

    const float JITTER_RADIUS = 14.0f;

    outResult.grassInstances.reserve((outResult.vertices.size() / stride) * bladesPerVertex);

    for (size_t i = 0; i < outResult.vertices.size(); i += stride) {
        const auto& v = outResult.vertices[i];

        if (v.normal.y > 0.75f && v.pos.y > -5.0f && v.color.y >= 0.8f) {
            for (int n = 0; n < bladesPerVertex; n++) {
                GrassInstance inst{};

                float jitterX = (fastRand(rngState) - 0.5f) * 2.0f * JITTER_RADIUS;
                float jitterZ = (fastRand(rngState) - 0.5f) * 2.0f * JITTER_RADIUS;

                float bladeX = v.pos.x + jitterX;
                float bladeZ = v.pos.z + jitterZ;
                float bladeY = Chunk::GetHeight(bladeX, bladeZ);

                inst.position = glm::vec3(bladeX, bladeY, bladeZ);
                inst.rotation = fastRand(rngState) * 2.0f * 3.14159f;

                float randVal = fastRand(rngState);

                float height = 1.2f + (randVal * 1.2f);
                float baseWidth = height * 1.35f;
                float finalWidth = baseWidth * widthMultiplier;

                inst.scale = glm::vec3(finalWidth, height, finalWidth);
                inst.windOffset = fastRand(rngState) * 50.0f;

                outResult.grassInstances.push_back(inst);
            }
        }
    }
}
void Chunk::GenerateChunkTrees(int chunkX, int chunkZ, int lod, ChunkJobResult& outResult) {
    // CPU CULL: Stop generating trees if the chunk is LOD 4 or higher (past treeFadeEnd)
    if (lod >= 4) {
        return;
    }

    // FIXED STEP SIZE: Every LOD evaluates exact same grid coordinates
    const float stepSize = 16.0f;

    const float startX = chunkX * m_chunkSize;
    const float startZ = chunkZ * m_chunkSize;

    static thread_local FastNoiseLite treeNoise;
    static thread_local bool treeNoiseInit = false;
    if (!treeNoiseInit) {
        treeNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        treeNoise.SetFrequency(0.003f);
        treeNoiseInit = true;
    }
    treeNoise.SetSeed(s_globalSeed + 420);

    for (float x = startX; x < startX + m_chunkSize; x += stepSize) {
        for (float z = startZ; z < startZ + m_chunkSize; z += stepSize) {

            float forestMask = treeNoise.GetNoise(x, z);

            if (forestMask > 0.1f) {
                uint32_t coordHash = Hash2D(static_cast<int>(x), static_cast<int>(z), s_globalSeed);
                float spawnChance = (coordHash % 1000) / 1000.0f;

                if (spawnChance < 0.15f) {
                    auto [groundY, biomeColor] = CalculateHeightAndColor(x, z);

                    if (groundY > 2.0f && groundY < 85.0f && biomeColor.y >= 0.7f) {
                        TreeInstance tree{};
                        tree.position = glm::vec3(x, groundY, z);
                        tree.rotation = glm::vec3(0.0f, static_cast<float>(coordHash % 360), 0.0f);

                        float randomScale = 0.08f + (static_cast<float>((coordHash >> 8) % 70) / 1000.0f);
                        tree.scale = glm::vec3(randomScale);

                        outResult.trees.push_back(tree);
                    }
                }
            }
        }
    }
}
void Chunk::GenerateChunkSwarms(int chunkX, int chunkZ, int lod, ChunkJobResult& outResult) {
    // Only spawn butterflies in high-detail chunks (LOD 0 or 1)
    if (lod >= 2) return;

    float centerWorldX = chunkX * m_chunkSize + (m_chunkSize * 0.5f);
    float centerWorldZ = chunkZ * m_chunkSize + (m_chunkSize * 0.5f);
    auto [groundY, biomeWeights] = CalculateHeightAndColor(centerWorldX, centerWorldZ);

    uint32_t coordHash = Hash2D(chunkX, chunkZ, s_globalSeed);

    // Spawn in lowlands/foothills with a 40% chance per chunk
    if (groundY < 80.0f && (coordHash % 100) < 40) {
        outResult.butterflies.reserve(46);
        for (int i = 0; i < 46; i++) {
            BoidInstance b{};
            float jitterX = ((coordHash * (i + 1) % 100) / 100.0f) * 30.0f - 15.0f;
            float jitterZ = ((coordHash * (i + 3) % 100) / 100.0f) * 30.0f - 15.0f;

            float randomScale = 0.5f + ((coordHash * (i + 7) % 100) / 100.0f) * 0.5f;

            b.position = glm::vec4(centerWorldX + jitterX, groundY + 4.0f + (i % 4), centerWorldZ + jitterZ, randomScale);

            // Randomize starting animation time (w component) so they don't flap in sync
            float randomTimeOffset = static_cast<float>((coordHash * i) % 1000);
            b.velocity = glm::vec4(1.0f, 0.0f, 0.0f, randomTimeOffset);

            outResult.butterflies.push_back(b);
        }
    }
}

void Chunk::UpdateFogParamsBasedOnData(VulkanRenderer& renderer) {
    float totalViewDistance = viewDistanceChunks * chunkSize;
    float fogStart = 0.3125f * totalViewDistance;
    float fogEnd = 0.3320f * totalViewDistance;
    renderer.SetFogParams(fogStart, fogEnd);
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