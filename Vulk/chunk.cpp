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
std::string ChunkName(int cx, int cz) {
    return "terrain_chunk_" + std::to_string(cx) + "_" + std::to_string(cz);
}
int Chunk::DesiredLodForDistance(float distance) const {
    if (distance < chunkSize)  return 0;
    if (distance < chunkSize * 1.5f)   return 1;
    if (distance < chunkSize * 3.0f)   return 2;
    if (distance < chunkSize * 5.0f)   return 3;
    return 4;
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
                //else {
                //    rComp.meshName = "assets/models/tree/tree_billboard.obj"; // 2-triangle cross plane
                //    tComp.scale = treeData.scale + glm::vec3(5);
                //}

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
            int64_t key = ChunkCoord{ cx, cz }.Key();

            if (m_desiredKeysLookup.find(key) != m_desiredKeysLookup.end()) continue;

            glm::vec3 center = ChunkBoundsCenter(cx, cz);
            float distanceToCam = glm::distance(center, camPos);
            int desiredLod = DesiredLodForDistance(distanceToCam);

            int targetResolution = resolution;
            if (desiredLod == 1)      targetResolution = ((resolution - 1) / 2) + 1;
            else if (desiredLod == 2) targetResolution = ((resolution - 1) / 4) + 1;

            if (loadedChunks.find(key) != loadedChunks.end() && loadedChunks[key] <= desiredLod) continue;
            if (loadingChunks.find(key) != loadingChunks.end() && loadingChunks[key] == desiredLod) continue;

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
                jobData.coord = ChunkCoord{ cx, cz };
                jobData.lod = desiredLod;

                PooledMeshBuffers buffers = m_bufferPool.Acquire();
                jobData.vertices = std::move(buffers.vertices);
                jobData.indices = std::move(buffers.indices);

                GenerateChunk(cx, cz, targetResolution, size, jobData, cancelToken);
                return jobData;
                });

            asyncResults.push_back({ std::move(future), cancelToken, key });
        }
    }
}

std::pair<float, glm::vec3> Chunk::CalculateHeightAndColor(float worldX, float worldZ) {
    static thread_local FastNoiseLite baseNoise;
    static thread_local FastNoiseLite tempNoise;
    static thread_local FastNoiseLite moistNoise;
    static thread_local FastNoiseLite lakeNoise;
    static thread_local FastNoiseLite warpNoise;
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

        lakeNoise.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
        lakeNoise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Euclidean);
        lakeNoise.SetCellularReturnType(FastNoiseLite::CellularReturnType_CellValue);
        lakeNoise.SetFrequency(0.0045f);

        warpNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        warpNoise.SetFrequency(0.01f);

        noiseInitialized = true;
    }

    int seed = s_globalSeed;
    baseNoise.SetSeed(seed);
    tempNoise.SetSeed(seed + 101);
    moistNoise.SetSeed(seed + 202);
    lakeNoise.SetSeed(seed + 606);

    const float COLD_BOUND = 0.3f;
    const float BLEND_RANGE = 0.04f;

    float rawBase = (baseNoise.GetNoise(worldX, worldZ) + 1.0f) * 0.5f;
    float t = (tempNoise.GetNoise(worldX, worldZ) + 1.0f) * 0.5f;
    float m = (moistNoise.GetNoise(worldX, worldZ) + 1.0f) * 0.5f;

    auto clamp01 = [](float val) { return std::max(0.0f, std::min(1.0f, val)); };

    BiomeProperties properties[4];
    properties[0] = GetBiomeProperties(DetermineBiome(clamp01(t - BLEND_RANGE), clamp01(m - BLEND_RANGE)));
    properties[1] = GetBiomeProperties(DetermineBiome(clamp01(t + BLEND_RANGE), clamp01(m - BLEND_RANGE)));
    properties[2] = GetBiomeProperties(DetermineBiome(clamp01(t - BLEND_RANGE), clamp01(m + BLEND_RANGE)));
    properties[3] = GetBiomeProperties(DetermineBiome(clamp01(t + BLEND_RANGE), clamp01(m + BLEND_RANGE)));

    float blendedScale = (properties[0].heightScale + properties[1].heightScale + properties[2].heightScale + properties[3].heightScale) * 0.25f;
    float blendedExp = (properties[0].exponent + properties[1].exponent + properties[2].exponent + properties[3].exponent) * 0.25f;
    glm::vec3 blendedWeights = (properties[0].textureWeights + properties[1].textureWeights + properties[2].textureWeights + properties[3].textureWeights) * 0.25f;

    float curvedNoise = std::pow(rawBase, blendedExp);
    float finalHeight = curvedNoise * blendedScale;

    if (t < COLD_BOUND) {
        float coldFactor = 1.0f - (t / COLD_BOUND);
        finalHeight += coldFactor * 20.0f;
    }

    return { finalHeight, blendedWeights };
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

    if (outResult.lod > 2) {
        return;
    }

    int stride = 2;
    int bladesPerVertex = 6;
    float widthMultiplier = 1.0f;

    if (outResult.lod == 1) {
        stride = 3;
        bladesPerVertex = 2;
        widthMultiplier = 2.0f;
    }
    else if (outResult.lod == 2) {
        stride = 6;
        bladesPerVertex = 1;
        widthMultiplier = 3.0f;
    }

    const float JITTER_RADIUS = 6.5f;

    outResult.grassInstances.reserve((outResult.vertices.size() / stride) * bladesPerVertex);

    for (size_t i = 0; i < outResult.vertices.size(); i += stride) {
        const auto& v = outResult.vertices[i];

        if (v.normal.y > 0.75f && v.pos.y > -5.0f && v.color.y >= 0.8f) {
            for (int n = 0; n < bladesPerVertex; n++) {
                GrassInstance inst;

                float jitterX = (fastRand(rngState) - 0.5f) * 2.0f * JITTER_RADIUS;
                float jitterZ = (fastRand(rngState) - 0.5f) * 2.0f * JITTER_RADIUS;

                inst.position = v.pos + glm::vec3(jitterX, 0.0f, jitterZ);
                inst.rotation = fastRand(rngState) * 2.0f * 3.14159f;

                float randVal = fastRand(rngState);

                float height = 1.5f + (randVal * 2.0f);
                float baseWidth = height * 1.2f;
                float finalWidth = baseWidth * widthMultiplier;

                inst.scale = glm::vec3(finalWidth, height, finalWidth);
                inst.windOffset = fastRand(rngState) * 50.0f;

                outResult.grassInstances.push_back(inst);
            }
        }
    }
}

void Chunk::GenerateChunkTrees(int chunkX, int chunkZ, int lod, ChunkJobResult& outResult) {
    // Adaptive density: Sparse up close for performance, dense far away for visuals
    float stepSize = 24.0f; // LOD 0 (High-poly close up)
    if (lod == 1) stepSize = 16.0f; // LOD 1 (Mid range)
    if (lod == 2) stepSize = 10.0f; // LOD 2 (Distant billboards - dense forest look)

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
                        TreeInstance tree;
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