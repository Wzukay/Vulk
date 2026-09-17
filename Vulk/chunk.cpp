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
    resolution = g_Settings.chunkResolution;

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
    float chunkRadius = chunkSize * 0.75f;
    float closestEdgeDist = std::max(0.0f, distToCenter - chunkRadius);

    if (closestEdgeDist > g_Settings.renderDistance) return 5;
    if (closestEdgeDist > g_Settings.GetTerrainLod3End()) return 4;
    if (closestEdgeDist > g_Settings.GetTerrainLod2End()) return 3;
    if (closestEdgeDist > g_Settings.GetTerrainLod1End()) return 2;
    if (closestEdgeDist > g_Settings.GetTerrainLod0End()) return 1;

    return 0;
}
void Chunk::PrecomputeChunkOffsets(int viewDistance) {
    if (!s_sortedChunkOffsets.empty()) return;

    for (int dz = -viewDistance; dz <= viewDistance; ++dz) {
        for (int dx = -viewDistance; dx <= viewDistance; ++dx) {
            s_sortedChunkOffsets.push_back({ dx, dz });
        }
    }

    std::sort(s_sortedChunkOffsets.begin(), s_sortedChunkOffsets.end(), [](const auto& a, const auto& b) {
        return (a.first * a.first + a.second * a.second) < (b.first * b.first + b.second * b.second);
        });
}
float Chunk::GetLodMorph(float closestEdgeDistance, int currentLod) const {
    float transitionWidth = std::min(chunkSize * 0.25f, 128.0f);
    float transitionEnd = 0.0f;

    switch (currentLod) {
    case 0: transitionEnd = g_Settings.GetTerrainLod0End(); break;
    case 1: transitionEnd = g_Settings.GetTerrainLod1End(); break;
    case 2: transitionEnd = g_Settings.GetTerrainLod2End(); break;
    case 3: transitionEnd = g_Settings.GetTerrainLod3End(); break;
    default: return 0.0f;
    }

    float transitionStart = transitionEnd - transitionWidth;
    return glm::clamp((closestEdgeDistance - transitionStart) / transitionWidth, 0.0f, 1.0f);
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

// BASELINE: The exact Update loop you had before things broke.
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
                rComp.meshHash = propData.lodGroupName.empty() ? 0 : StringHash::Hash(propData.lodGroupName);
                rComp.type = MeshType::Static;
                rComp.isInstanced = true;
                rComp.isVisible = true;

                ChunkPropComponent cComp;
                cComp.chunkKey = resultKey;

                // --- BULLETPROOF COLLIDER ASSIGNMENT ---
                PropCollider baseCol = GetPropCollider(propData.lodGroupName);
                ColliderComponent colComp;
                colComp.type = baseCol.type;

                if (colComp.type == ColliderType::Cylinder) {
                    colComp.radius = baseCol.baseRadius * propData.scale.x;
                    colComp.height = baseCol.baseHeight * propData.scale.y;
                }
                else if (colComp.type == ColliderType::Box) {
                    colComp.halfExtents = baseCol.baseHalfExtents * propData.scale;
                }
                else if (colComp.type == ColliderType::Sphere) {
                    colComp.radius = baseCol.baseRadius * propData.scale.x;
                }

                // Attach everything to the ECS registry
                scene.GetRegistry().AddComponent<TransformComponent>(propEntity, tComp);
                scene.GetRegistry().AddComponent<RenderComponent>(propEntity, rComp);
                scene.GetRegistry().AddComponent<ChunkPropComponent>(propEntity, cComp);
                scene.GetRegistry().AddComponent<ColliderComponent>(propEntity, colComp);

                staticSceneChanged = true;
            }

            if (staticSceneChanged) scene.MarkStaticDirty();

            PublishGridCache(resultKey, std::move(result.physicsGrid));

            renderer.AddTerrainChunk(resultKey, result.coord.cx, result.coord.cz, result.lod, result.vertices, result.indices);

            if (!result.grassInstances.empty()) renderer.AddGrass(resultKey, result.grassInstances);
            else renderer.RemoveGrass(resultKey);

            if (!result.swarms.empty()) renderer.AddSwarms(resultKey, result.swarms);
            else renderer.RemoveSwarms(resultKey);

            if (!result.waterVertices.empty()) renderer.AddWaterChunk(resultKey, result.waterVertices, result.waterIndices);
            else renderer.RemoveWaterChunk(resultKey);

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

        for (const auto& offset : s_sortedChunkOffsets) {
            ChunkCoord coord{ camChunkX + offset.first, camChunkZ + offset.second };
            if (DesiredLodForDistance(glm::distance(ChunkBoundsCenter(coord.cx, coord.cz), camPos)) >= 5) continue;
            int64_t k = coord.Key();
            m_desiredKeys.push_back(k);
            m_desiredKeysLookup.insert(k);
            m_desiredList.emplace_back(k, std::make_pair(coord.cx, coord.cz));
        }

        m_currentAmortizeIndex = 0;
        m_needsGridRebuild = true;

        for (auto it = loadedChunks.begin(); it != loadedChunks.end(); ) {
            if (m_desiredKeysLookup.find(it->first) == m_desiredKeysLookup.end()) {
                RemoveGridCache(it->first);
                renderer.RemoveTerrainChunk(it->first);
                renderer.RemoveGrass(it->first);
                renderer.RemoveSwarms(it->first);
                renderer.RemoveWaterChunk(it->first);
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
                    if (job.key == keyToCancel && job.cancelToken) {
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
                        if (activeJob.key == itemKey && activeJob.cancelToken) {
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
    static thread_local FastNoiseLite baseNoise;
    static thread_local FastNoiseLite ridgeNoise;
    static thread_local FastNoiseLite detailNoise;
    static thread_local FastNoiseLite tempNoise;
    static thread_local FastNoiseLite moistNoise;
    static thread_local FastNoiseLite lakeNoise;
    static thread_local bool initialized = false;

    if (!initialized) {
        baseNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        baseNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        baseNoise.SetFractalOctaves(4);
        baseNoise.SetFrequency(0.0008f);

        ridgeNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        ridgeNoise.SetFractalType(FastNoiseLite::FractalType_Ridged);
        ridgeNoise.SetFractalOctaves(4);
        ridgeNoise.SetFrequency(0.0015f);

        detailNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        detailNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        detailNoise.SetFractalOctaves(3);
        detailNoise.SetFrequency(0.015f);

        tempNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        tempNoise.SetFrequency(0.00028f);

        moistNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        moistNoise.SetFrequency(0.00036f);

        lakeNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        lakeNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        lakeNoise.SetFractalOctaves(2);
        lakeNoise.SetFrequency(0.002f);

        initialized = true;
    }

    const int seed = s_globalSeed;
    baseNoise.SetSeed(seed + 11);
    ridgeNoise.SetSeed(seed + 37);
    detailNoise.SetSeed(seed + 89);
    tempNoise.SetSeed(seed + 101);
    moistNoise.SetSeed(seed + 202);
    lakeNoise.SetSeed(seed + 999);

    auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
    auto n01 = [](float n) { return (n + 1.0f) * 0.5f; };

    float base = n01(baseNoise.GetNoise(worldX, worldZ));
    float ridge = n01(ridgeNoise.GetNoise(worldX, worldZ));
    float detail = n01(detailNoise.GetNoise(worldX, worldZ));

    float height = 25.0f;
    float plainsHeight = base * 25.0f;
    float mountainMask = glm::smoothstep(0.4f, 0.7f, base);
    float mountainHeight = std::pow(ridge, 2.0f) * 140.0f;

    height += std::lerp(plainsHeight, plainsHeight + mountainHeight, mountainMask);
    height += (detail * 4.0f) * (1.0f - mountainMask * 0.5f);

    float t = clamp01(n01(tempNoise.GetNoise(worldX, worldZ)));
    float m = clamp01(n01(moistNoise.GetNoise(worldX, worldZ)));

    struct BiomeCenter { BiomeType type; float t; float m; };
    const BiomeCenter centers[] = {
        {BiomeType::Desert,     0.83f, 0.125f}, {BiomeType::Savanna, 0.83f, 0.375f}, {BiomeType::Jungle,     0.83f, 0.65f}, {BiomeType::Swamp,      0.83f, 0.90f},
        {BiomeType::Shrubland,  0.50f, 0.125f}, {BiomeType::Plains,  0.50f, 0.375f}, {BiomeType::Forest,     0.50f, 0.65f}, {BiomeType::DeepForest, 0.50f, 0.90f},
        {BiomeType::Tundra,     0.16f, 0.125f}, {BiomeType::Taiga,   0.16f, 0.375f}, {BiomeType::SnowWastes, 0.16f, 0.65f}, {BiomeType::Alpine,     0.16f, 0.90f}
    };

    glm::vec3 blendedWeights(0.0f);
    glm::vec3 blendedColor(0.0f);
    float totalWeight = 0.0f;

    for (int i = 0; i < 12; ++i) {
        float dt = t - centers[i].t;
        float dm = m - centers[i].m;
        float distSq = dt * dt + dm * dm;

        float weight = 1.0f / (distSq * distSq + 0.0001f);

        const BiomeDefinition& def = GetBiomeDefinition(centers[i].type);
        blendedWeights += def.textureWeights * weight;
        blendedColor += def.groundColor * weight;
        totalWeight += weight;
    }

    blendedWeights /= totalWeight;
    blendedColor /= totalWeight;

    float lakeWaterLevel = 28.0f;
    float lakeMaxMacroHeight = 42.0f;
    const float BASIN_THRESHOLD = 0.25f;
    const float WATER_MESH_THRESHOLD = 0.25f;
    float lakeMask = 0.0f;

    float hugeLakeNoise = n01(lakeNoise.GetNoise(worldX * 0.12f, worldZ * 0.12f));
    float hugeLakeMask = glm::smoothstep(0.68f, 0.85f, hugeLakeNoise);
    float rawLakeMask = hugeLakeMask;
    float macroHeight = 25.0f + plainsHeight;

    if (rawLakeMask > 0.0f && macroHeight <= lakeMaxMacroHeight) {
        float heightFade = 1.0f - glm::smoothstep(lakeWaterLevel + 1.0f, lakeMaxMacroHeight, macroHeight);
        lakeMask = rawLakeMask * heightFade;

        float localDetail = std::abs(detail * 2.0f - 1.0f);
        float mountainSteepness = mountainMask * 25.0f;
        float terrainSteepness = mountainSteepness + localDetail * 8.0f;
        float slopeMask = 1.0f - glm::smoothstep(12.0f, 25.0f, terrainSteepness);
        float basinMask = lakeMask * slopeMask;

        if (basinMask > BASIN_THRESHOLD) {
            float lakeDepth = 14.0f;
            float lakeBed = lakeWaterLevel - lakeDepth;
            float basinDeformation = glm::smoothstep(BASIN_THRESHOLD, BASIN_THRESHOLD + 0.35f, basinMask);

            height = std::lerp(height, lakeBed, basinDeformation);
            blendedColor = glm::mix(blendedColor, glm::vec3(0.20f, 0.18f, 0.15f), basinDeformation);
            blendedWeights = glm::mix(blendedWeights, glm::vec3(1.0f, 0.0f, 0.0f), basinDeformation);
        }
    }

    return { height, blendedWeights, blendedColor, lakeMask > WATER_MESH_THRESHOLD ? lakeWaterLevel : 0.0f };
}

float Chunk::GetHeight(float worldX, float worldZ) {
    return CalculateHeightAndColor(worldX, worldZ).height;
}
float Chunk::GetWaterLevel(float worldX, float worldZ) {
    return CalculateHeightAndColor(worldX, worldZ).waterLevel;
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
    ChunkJobResult& outResult, std::shared_ptr<std::atomic<bool>> cancelToken)
{
    outResult.vertices.clear();
    outResult.indices.clear();
    outResult.grassInstances.clear();
    outResult.props.clear();
    outResult.swarms.clear();

    outResult.vertices.reserve(resolution * resolution + (resolution - 1) * 8);
    outResult.indices.reserve(((resolution - 1) * (resolution - 1) * 6) + ((resolution - 1) * 4 * 6));

    const float step = chunkSize / (float)(resolution - 1);
    float originX = chunkX * chunkSize;
    float originZ = chunkZ * chunkSize;

    int pad = 2;
    int gridSize = resolution + pad * 2;

    std::vector<float> waterGrid(gridSize * gridSize);
    std::vector<float> heightGrid(gridSize * gridSize);
    std::vector<glm::vec3> colorGrid(gridSize * gridSize);
    std::vector<glm::vec3> groundColorGrid(gridSize * gridSize);

    for (int gz = 0; gz < gridSize; ++gz) {
        const float worldZ = originZ + (gz - pad) * step;
        const int rowOffset = gz * gridSize;

        for (int gx = 0; gx < gridSize; ++gx) {
            const float worldX = originX + (gx - pad) * step;
            TerrainData data = CalculateHeightAndColor(worldX, worldZ);

            waterGrid[rowOffset + gx] = data.waterLevel;
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
    auto GetCachedWater = [&](int localX, int localZ) -> float {
        int cx = std::clamp(localX + pad, 0, gridSize - 1);
        int cz = std::clamp(localZ + pad, 0, gridSize - 1);
        return waterGrid[cz * gridSize + cx];
        };

    for (int z = 0; z < resolution; z++) {
        if (cancelToken && cancelToken->load(std::memory_order_relaxed)) return;

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
            v.texCoord = glm::packHalf2x16(glm::vec2(worldX * 0.02f, worldZ * 0.02f));

            glm::vec3 biomeWeights = GetCachedColor(x, z);

            glm::vec3 tangentX(2.0f * step, hR - hL, 0.0f);
            glm::vec3 tangentZ(0.0f, hU - hD, 2.0f * step);
            glm::vec3 calculatedNormal = glm::normalize(glm::cross(tangentZ, tangentX));
            glm::vec3 t = glm::normalize(tangentX);
            t = glm::normalize(t - calculatedNormal * glm::dot(calculatedNormal, t));

            v.normal = EncodeNormal(calculatedNormal);
            v.tangent = EncodeTangent(glm::vec4(biomeWeights, 1.0f));

            int cx = ((x + 1) / 2) * 2;
            int cz = ((z + 1) / 2) * 2;

            float hCoarse = GetCachedHeight(cx, cz);
            glm::vec3 coarseBiomeWeights = GetCachedColor(cx, cz);

            float hL_coarse = GetCachedHeight(cx - 2, cz);
            float hR_coarse = GetCachedHeight(cx + 2, cz);
            float hD_coarse = GetCachedHeight(cx, cz - 2);
            float hU_coarse = GetCachedHeight(cx, cz + 2);

            glm::vec3 coarseTangentX(4.0f * step, hR_coarse - hL_coarse, 0.0f);
            glm::vec3 coarseTangentZ(0.0f, hU_coarse - hD_coarse, 4.0f * step);
            glm::vec3 coarseNormal = glm::normalize(glm::cross(coarseTangentZ, coarseTangentX));
            glm::vec3 calculatedCoarseNormal = glm::normalize(glm::cross(coarseTangentZ, coarseTangentX));

            v.coarseNormal = EncodeNormal(calculatedCoarseNormal);
            v.coarseTangent = EncodeTangent(glm::vec4(coarseBiomeWeights, 1.0f));
            v.coarseY = hCoarse;

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

    const float terrainSkirtDepth = 48.0f;

    auto addTerrainSkirtSegment = [&](uint32_t topA, uint32_t topB) {
        ModelVertex skirtA = outResult.vertices[topA];
        ModelVertex skirtB = outResult.vertices[topB];

        skirtA.pos.y -= terrainSkirtDepth;
        skirtB.pos.y -= terrainSkirtDepth;
        skirtA.coarseY -= terrainSkirtDepth;
        skirtB.coarseY -= terrainSkirtDepth;

        uint32_t skirtIndexA = static_cast<uint32_t>(outResult.vertices.size());
        outResult.vertices.push_back(skirtA);

        uint32_t skirtIndexB = static_cast<uint32_t>(outResult.vertices.size());
        outResult.vertices.push_back(skirtB);

        outResult.indices.insert(outResult.indices.end(), {
            topA,
            skirtIndexA,
            topB,

            topB,
            skirtIndexA,
            skirtIndexB
            });
        };

    for (int x = 0; x < resolution - 1; ++x) {
        addTerrainSkirtSegment(x, x + 1);
    }

    for (int z = 0; z < resolution - 1; ++z) {
        addTerrainSkirtSegment(z * resolution + resolution - 1, (z + 1) * resolution + resolution - 1);
    }

    for (int x = resolution - 1; x > 0; --x) {
        addTerrainSkirtSegment((resolution - 1) * resolution + x, (resolution - 1) * resolution + x - 1);
    }

    for (int z = resolution - 1; z > 0; --z) {
        addTerrainSkirtSegment(z * resolution, (z - 1) * resolution);
    }

    // --- 4.5 GENERATE MARCHING-SQUARES LAKE MESH ---
    outResult.waterVertices.clear();
    outResult.waterIndices.clear();

    std::unordered_map<uint64_t, uint32_t> waterVertIndices;

    const int waterSubdivisions = 1;
    const int waterResolution = (resolution - 1) * waterSubdivisions + 1;
    const float waterGridScale = 1.0f / static_cast<float>(waterSubdivisions);
    const int waterStep = 1;

    std::vector<TerrainData> waterSamples(waterResolution * waterResolution);

    for (int z = 0; z < waterResolution; ++z) {
        for (int x = 0; x < waterResolution; ++x) {
            float terrainGridX = static_cast<float>(x) * waterGridScale;
            float terrainGridZ = static_cast<float>(z) * waterGridScale;

            float worldX = originX + terrainGridX * step;
            float worldZ = originZ + terrainGridZ * step;

            waterSamples[z * waterResolution + x] = CalculateHeightAndColor(worldX, worldZ);
        }
    }

    auto getWaterSample = [&](int x, int z) -> const TerrainData& {
        return waterSamples[z * waterResolution + x];
        };

    auto addWaterVert = [&](const glm::vec2& gridPos, float waterHeight) {
        float worldX = originX + gridPos.x * step;
        float worldZ = originZ + gridPos.y * step;

        const float SNAP_RESOLUTION = 0.5f;
        uint32_t xKey = static_cast<uint32_t>(std::round(worldX / SNAP_RESOLUTION));
        uint32_t zKey = static_cast<uint32_t>(std::round(worldZ / SNAP_RESOLUTION));
        uint64_t key = (static_cast<uint64_t>(xKey) << 32) | zKey;

        auto existing = waterVertIndices.find(key);
        if (existing != waterVertIndices.end()) {
            return existing->second;
        }

        TerrainData terrainData = CalculateHeightAndColor(worldX, worldZ);
        float terrainHeight = terrainData.height;

        ModelVertex vertex{};
        vertex.pos = glm::vec3(worldX, waterHeight, worldZ);
        vertex.normal = EncodeNormal(glm::vec3(0.0f, 1.0f, 0.0f));

        float rawDepth = waterHeight - terrainHeight;
        float normalizedDepth = std::clamp(rawDepth / 30.0f, 0.0f, 1.0f);
        vertex.tangent = EncodeTangent(glm::vec4(1.0f, 0.0f, 0.0f, normalizedDepth));

        vertex.coarseY = vertex.pos.y;
        vertex.coarseNormal = vertex.normal;
        vertex.coarseTangent = vertex.tangent;

        uint32_t index = static_cast<uint32_t>(outResult.waterVertices.size());
        outResult.waterVertices.push_back(vertex);
        waterVertIndices[key] = index;

        return index;
        };

    auto emitPolygon = [&](std::initializer_list<glm::vec2> points, float waterHeight) {
        if (points.size() < 3) {
            return;
        }

        auto point = points.begin();

        uint32_t first = addWaterVert(*point, waterHeight);
        uint32_t previous = addWaterVert(*(point + 1), waterHeight);

        for (size_t i = 2; i < points.size(); ++i) {
            uint32_t current = addWaterVert(*(point + i), waterHeight);

            outResult.waterIndices.insert(outResult.waterIndices.end(), {
                first,
                previous,
                current
                });

            previous = current;
        }
        };

    for (int z = 0; z < waterResolution - 1; z += waterStep) {
        if (cancelToken && cancelToken->load(std::memory_order_relaxed)) return;

        const int z1 = std::min(z + waterStep, waterResolution - 1);

        for (int x = 0; x < waterResolution - 1; x += waterStep) {
            const int x1 = std::min(x + waterStep, waterResolution - 1);

            const float w00 = getWaterSample(x, z).waterLevel;
            const float w10 = getWaterSample(x1, z).waterLevel;
            const float w01 = getWaterSample(x, z1).waterLevel;
            const float w11 = getWaterSample(x1, z1).waterLevel;

            int mask = 0;

            if (w00 > 0.0f) mask |= 1;
            if (w10 > 0.0f) mask |= 2;
            if (w11 > 0.0f) mask |= 4;
            if (w01 > 0.0f) mask |= 8;

            if (mask == 0) {
                continue;
            }

            float waterHeight = std::max(std::max(w00, w10), std::max(w11, w01));

            glm::vec2 p00(static_cast<float>(x) * waterGridScale, static_cast<float>(z) * waterGridScale);
            glm::vec2 p10(static_cast<float>(x1) * waterGridScale, static_cast<float>(z) * waterGridScale);
            glm::vec2 p11(static_cast<float>(x1) * waterGridScale, static_cast<float>(z1) * waterGridScale);
            glm::vec2 p01(static_cast<float>(x) * waterGridScale, static_cast<float>(z1) * waterGridScale);

            glm::vec2 e0 = (p00 + p10) * 0.5f;
            glm::vec2 e1 = (p10 + p11) * 0.5f;
            glm::vec2 e2 = (p11 + p01) * 0.5f;
            glm::vec2 e3 = (p01 + p00) * 0.5f;

            switch (mask) {
            case 1:  emitPolygon({ p00, e0, e3 }, waterHeight); break;
            case 2:  emitPolygon({ p10, e1, e0 }, waterHeight); break;
            case 3:  emitPolygon({ p00, p10, e1, e3 }, waterHeight); break;
            case 4:  emitPolygon({ p11, e2, e1 }, waterHeight); break;
            case 5:  emitPolygon({ p00, e0, e3 }, waterHeight); emitPolygon({ p11, e2, e1 }, waterHeight); break;
            case 6:  emitPolygon({ p10, p11, e2, e0 }, waterHeight); break;
            case 7:  emitPolygon({ p00, p10, p11, e2, e3 }, waterHeight); break;
            case 8:  emitPolygon({ p01, e3, e2 }, waterHeight); break;
            case 9:  emitPolygon({ p00, e0, e2, p01 }, waterHeight); break;
            case 10: emitPolygon({ p10, e1, e0 }, waterHeight); emitPolygon({ p01, e3, e2 }, waterHeight); break;
            case 11: emitPolygon({ p00, p10, e1, e2, p01 }, waterHeight); break;
            case 12: emitPolygon({ p01, p11, e1, e3 }, waterHeight); break;
            case 13: emitPolygon({ p00, e0, e1, p11, p01 }, waterHeight); break;
            case 14: emitPolygon({ e0, p10, p11, p01, e3 }, waterHeight); break;
            case 15: emitPolygon({ p00, p10, p11, p01 }, waterHeight); break;
            }
        }
    }

    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) return;

    if (!outResult.indices.empty()) {
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
    }

    auto GetFastLocalData = [&](float wX, float wZ) -> TerrainData {
        float localX = wX - originX;
        float localZ = wZ - originZ;

        if (localX < 0.0f || localX >= m_chunkSize || localZ < 0.0f || localZ >= m_chunkSize) {
            return CalculateHeightAndColor(wX, wZ);
        }

        float gridMax = static_cast<float>(std::max(1, resolution - 1));
        float gridX = (localX / m_chunkSize) * gridMax;
        float gridZ = (localZ / m_chunkSize) * gridMax;

        int maxIndex0 = std::max(0, resolution - 2);
        int maxIndex1 = std::max(0, resolution - 1);

        int x0 = std::clamp(static_cast<int>(gridX), 0, maxIndex0);
        int z0 = std::clamp(static_cast<int>(gridZ), 0, maxIndex0);
        int x1 = std::min(x0 + 1, maxIndex1);
        int z1 = std::min(z0 + 1, maxIndex1);

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

        float water00 = GetCachedWater(x0, z0);
        float water10 = GetCachedWater(x1, z0);
        float water01 = GetCachedWater(x0, z1);
        float water11 = GetCachedWater(x1, z1);

        float water0 = std::lerp(water00, water10, tx);
        float water1 = std::lerp(water01, water11, tx);
        float finalWater = std::lerp(water0, water1, tz);

        return { finalH, finalW, finalC, finalWater };
        };

    GenerateChunkProps(chunkX, chunkZ, outResult.lod, outResult, GetFastLocalData);
    GenerateChunkSwarms(chunkX, chunkZ, outResult.lod, outResult, GetFastLocalData);
    GenerateChunkStructures(chunkX, chunkZ, outResult.lod, outResult, GetFastLocalData);

    if (outResult.lod == 0) {
        const float GRASS_STEP = 8.0f;
        int bladesPerCell = 18;
        const float JITTER_RADIUS = 4.0f;

        int gridCells = static_cast<int>(chunkSize / GRASS_STEP);
        outResult.grassInstances.reserve(gridCells * gridCells * bladesPerCell);

        for (float localX = 0.0f; localX < chunkSize; localX += GRASS_STEP) {
            if (cancelToken && cancelToken->load(std::memory_order_relaxed)) return;

            for (float localZ = 0.0f; localZ < chunkSize; localZ += GRASS_STEP) {

                float baseX = originX + localX;
                float baseZ = originZ + localZ;

                TerrainData data = GetFastLocalData(baseX, baseZ);

                if (data.height <= -5.0f || data.height > 60.0f || data.waterLevel > 0.0f || data.biomeWeights.y < 0.2f) continue;

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
            float worldX = chunkX * chunkSize + (ix * stepSize);
            float worldZ = chunkZ * chunkSize + (iz * stepSize);

            BiomeType biome = GetDominantBiome(worldX, worldZ);
            const BiomeDefinition& def = GetBiomeDefinition(biome);
            if (def.props.empty()) continue;

            uint32_t coordHash = Hash2D(chunkX * 1000 + ix, chunkZ * 1000 + iz, s_globalSeed);
            TerrainData data = heightColorFunc(worldX, worldZ);

            if (data.waterLevel > 0.0f) continue;

            // --- NEW: Calculate the terrain normal to check for steep slopes ---
            float hR = heightColorFunc(worldX + 2.0f, worldZ).height;
            float hU = heightColorFunc(worldX, worldZ + 2.0f).height;
            glm::vec3 normal = glm::normalize(glm::vec3(data.height - hR, 2.0f, data.height - hU));

            float rawTree = treeNoise.GetNoise(worldX, worldZ);
            float rawStone = stoneNoise.GetNoise(worldX, worldZ);
            float masks[2] = { (rawTree + 1.0f) * 0.5f, (rawStone + 1.0f) * 0.5f };

            uint32_t ruleHash = coordHash;

            for (const auto& rule : def.props) {
                if (lod > rule.maxLod) continue;
                if (data.height < rule.minHeight || data.height > rule.maxHeight) continue;
                if (masks[rule.noiseIndex] < rule.noiseThreshold) continue;

                // --- NEW: Prevent trees from spawning on slopes steeper than ~31 degrees ---
                if (rule.lodGroupName == "TreeGroup" && normal.y < 0.85f) continue;

                ruleHash ^= ruleHash << 13; ruleHash ^= ruleHash >> 17; ruleHash ^= ruleHash << 5;
                float ruleRoll = (ruleHash % 1000) / 1000.0f;

                if (ruleRoll < rule.spawnChance) {
                    PropInstance prop{};
                    prop.position = glm::vec3(worldX, data.height + rule.groundOffset, worldZ);

                    prop.rotation = glm::vec3(
                        rule.alignToNormal ? static_cast<float>(ruleHash % 360) : 0.0f,
                        static_cast<float>((ruleHash >> 4) % 360),
                        rule.alignToNormal ? static_cast<float>((ruleHash >> 8) % 360) : 0.0f
                    );

                    float scaleT = static_cast<float>((ruleHash >> 8) % 100) / 100.0f;
                    prop.scale = glm::vec3(std::lerp(rule.minScale, rule.maxScale, scaleT));
                    prop.customPayload = static_cast<float>(ruleHash % 100) / 100.0f;
                    prop.lodGroupName = rule.lodGroupName;

                    outResult.props.push_back(prop);
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
    if (data.waterLevel > 0.0f) return;

    uint32_t coordHash = Hash2D(chunkX, chunkZ, s_globalSeed);
    float spawnChance = (coordHash % 1000) / 1000.0f;

    for (const auto& rule : def.swarms) {
        if (data.height < rule.minHeight || data.height > rule.maxHeight) continue;

        if (spawnChance < rule.spawnChance) {
            SwarmData swarmData;
            swarmData.behavior = rule.behavior;
            swarmData.instances.reserve(rule.boidCount);

            if (swarmData.behavior.animationType == 0) { // 0 = Butterfly
                AssetRecord* rec = g_AssetManager.GetAssetRecord("butterfly");
                swarmData.behavior.textureId = rec ? rec->resourceId : 0;
            }

            for (int i = 0; i < rule.boidCount; i++) {
                BoidInstance b{};
                float jitterX = ((coordHash * (i + 1) % 100) / 100.0f) * (rule.spreadRadius * 2) - rule.spreadRadius;
                float jitterZ = ((coordHash * (i + 3) % 100) / 100.0f) * (rule.spreadRadius * 2) - rule.spreadRadius;

                float boidX = centerWorldX + jitterX;
                float boidZ = centerWorldZ + jitterZ;

                TerrainData boidData = heightColorFunc(boidX, boidZ);
                if (boidData.waterLevel > 0.0f) continue;

                float scaleT = ((coordHash * (i + 7) % 100) / 100.0f);
                float randomScale = std::lerp(rule.minScale, rule.maxScale, scaleT);

                b.position = glm::vec4(boidX, boidData.height + rule.verticalOffset + (i % 4), boidZ, randomScale);
                float randomTimeOffset = static_cast<float>((coordHash * i) % 1000);
                b.velocity = glm::vec4(1.0f, 0.0f, 0.0f, randomTimeOffset);

                swarmData.instances.push_back(b);
            }
            if (!swarmData.instances.empty()) outResult.swarms.push_back(swarmData);
        }
    }
}
void Chunk::GenerateChunkStructures(int chunkX, int chunkZ, int lod, ChunkJobResult& outResult,
    const std::function<TerrainData(float, float)>& heightColorFunc) {

    // Only generate structures at LOD 0, 1, or 2
    if (lod > 2) return;

    float centerWorldX = chunkX * m_chunkSize + (m_chunkSize * 0.5f);
    float centerWorldZ = chunkZ * m_chunkSize + (m_chunkSize * 0.5f);

    BiomeType biome = GetDominantBiome(centerWorldX, centerWorldZ);
    const BiomeDefinition& def = GetBiomeDefinition(biome);

    if (def.structures.empty()) return;

    // Deterministic hash for this chunk
    uint32_t chunkHash = Hash2D(chunkX, chunkZ, s_globalSeed + 9999);
    float spawnRoll = (chunkHash % 1000) / 1000.0f;

    float cumulativeChance = 0.0f;

    for (const auto& rule : def.structures) {
        cumulativeChance += rule.spawnChance;

        if (spawnRoll < cumulativeChance) {
            TerrainData centerData = heightColorFunc(centerWorldX, centerWorldZ);
            if (centerData.waterLevel > 0.0f) return; // No underwater camps

            std::cout << "[World Gen] " << rule.structure.name << " spawned at X: "
                << centerWorldX << " Z: " << centerWorldZ << "\n";

            // 1. SPAWN THE CENTRAL PIECE (Campfire)
            PropInstance centerProp{};
            centerProp.position = glm::vec3(centerWorldX, centerData.height, centerWorldZ);
            centerProp.rotation = glm::vec3(0.0f, static_cast<float>(chunkHash % 360), 0.0f);
            centerProp.scale = glm::vec3(rule.structure.centralScale);
            centerProp.lodGroupName = rule.structure.centralLodGroup;
            centerProp.customPayload = 0.0f;
            outResult.props.push_back(centerProp);

            // 2. SPAWN THE PERIPHERALS (Tents)
            // Use a separate hash state so we can safely advance it for multiple peripherals
            uint32_t pHash = Hash2D(chunkX, chunkZ, s_globalSeed + 7777);

            for (const auto& periph : rule.structure.peripherals) {
                int countRange = periph.maxCount - periph.minCount + 1;
                int actualCount = periph.minCount + (pHash % countRange);

                // Fast Xorshift to advance the random state
                pHash ^= pHash << 13; pHash ^= pHash >> 17; pHash ^= pHash << 5;

                for (int i = 0; i < actualCount; i++) {
                    // Random Angle
                    float angle = (pHash % 360) * (3.14159f / 180.0f);
                    pHash ^= pHash << 13; pHash ^= pHash >> 17; pHash ^= pHash << 5;

                    // Random Radius
                    float rFrac = (pHash % 100) / 100.0f;
                    pHash ^= pHash << 13; pHash ^= pHash >> 17; pHash ^= pHash << 5;
                    float radius = std::lerp(periph.minRadius, periph.maxRadius, rFrac);

                    // Calculate World Position
                    float pX = centerWorldX + cos(angle) * radius;
                    float pZ = centerWorldZ + sin(angle) * radius;

                    TerrainData pData = heightColorFunc(pX, pZ);
                    if (pData.waterLevel > 0.0f) continue;

                    // Random Scale
                    float sFrac = (pHash % 100) / 100.0f;
                    pHash ^= pHash << 13; pHash ^= pHash >> 17; pHash ^= pHash << 5;
                    float scale = std::lerp(periph.scaleMin, periph.scaleMax, sFrac);

                    // Calculate Rotation so the "tents" face the "campfire"
                    float facingAngle = atan2(centerWorldX - pX, centerWorldZ - pZ);

                    PropInstance pProp{};
                    pProp.position = glm::vec3(pX, pData.height, pZ);
                    pProp.rotation = glm::vec3(0.0f, glm::degrees(facingAngle), 0.0f);
                    pProp.scale = glm::vec3(scale);
                    pProp.lodGroupName = periph.lodGroupName;
                    pProp.customPayload = 0.0f;

                    outResult.props.push_back(pProp);
                }
            }

            // Only spawn one structure per chunk to prevent overlap
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