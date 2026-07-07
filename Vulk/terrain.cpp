#include "terrain.h"

void Terrain::GenerateChunk(int chunkX, int chunkZ, int resolution, float chunkSize, int seed,
    std::vector<ModelVertex>& outVertices, std::vector<uint32_t>& outIndices,
    std::shared_ptr<std::atomic<bool>> cancelToken) {

    outVertices.clear();
    outVertices.reserve(resolution * resolution + (resolution - 1) * 8);
    outIndices.clear();
    outIndices.reserve(((resolution - 1) * (resolution - 1) * 6) + ((resolution - 1) * 4 * 6));

    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) return;

    const float step = chunkSize / (float)(resolution - 1);
    float originX = chunkX * chunkSize;
    float originZ = chunkZ * chunkSize;

    int gridSize = resolution + 2;
    std::vector<float> heightGrid(gridSize * gridSize);
    std::vector<glm::vec3> colorGrid(gridSize * gridSize);

    static thread_local FastNoiseLite baseNoise;       // For basic terrain features
    static thread_local FastNoiseLite tempNoise;       // Low frequency noise for Temperature
    static thread_local FastNoiseLite moistNoise;      // Low frequency noise for Humidity
    static thread_local bool noiseInitialized = false;

    if (!noiseInitialized) {
        // Base Elevation Settings
        baseNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        baseNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        baseNoise.SetFractalOctaves(5);
        baseNoise.SetFrequency(0.002f);

        // Temperature Settings (Needs very low frequency so biomes are massive)
        tempNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        tempNoise.SetFrequency(0.0003f); // 👈 Huge, sweeping climates

        // Moisture Settings
        moistNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        moistNoise.SetFrequency(0.0004f);

        noiseInitialized = true;
    }

    // Always update seed to match current world criteria
    baseNoise.SetSeed(seed);
    tempNoise.SetSeed(seed + 101);
    moistNoise.SetSeed(seed + 202);

    const float COLD_BOUND = 0.3f;
    const float BLEND_RANGE = 0.5f;

    // 🚀 High-speed sequential data layout loop (Perfect for Compiler Auto-Vectorization)
    const float terrainHeightScale = 120.0f;
    for (int gz = 0; gz < gridSize; ++gz) {
        float worldZ = originZ + (gz - 1) * step;
        int rowOffset = gz * gridSize;

        for (int gx = 0; gx < gridSize; ++gx) {
            float worldX = originX + (gx - 1) * step;

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

            // 🚀 Smoothly blend the texture weights vectors over boundaries
            glm::vec3 blendedWeights = (properties[0].textureWeights + properties[1].textureWeights + properties[2].textureWeights + properties[3].textureWeights) * 0.25f;

            float curvedNoise = std::pow(rawBase, blendedExp);
            float finalHeight = curvedNoise * blendedScale;

            if (t < COLD_BOUND) {
                float coldFactor = 1.0f - (t / COLD_BOUND);
                finalHeight += coldFactor * 20.0f;
            }

            heightGrid[rowOffset + gx] = finalHeight;
            colorGrid[rowOffset + gx] = blendedWeights; // 🚀 Saved into the cache grid to become v.color below!
        }
    }

    auto GetCachedHeight = [&](int localX, int localZ) -> float {
        return heightGrid[(localZ + 1) * gridSize + (localX + 1)];
        };

    auto GetCachedColor = [&](int localX, int localZ) -> glm::vec3 {
        return colorGrid[(localZ + 1) * gridSize + (localX + 1)];
        };

    // --- Vertex and Index Assembly Loop ---
    for (int z = 0; z < resolution; z++) {
        if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
            outVertices.clear(); outIndices.clear(); return;
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
            v.color = GetCachedColor(x, z); // 🚀 FIX: Assign your smooth biome color!

            glm::vec3 tangentX(2.0f * step, hR - hL, 0.0f);
            glm::vec3 tangentZ(0.0f, hU - hD, 2.0f * step);
            v.normal = glm::normalize(glm::cross(tangentZ, tangentX));

            glm::vec3 t = glm::normalize(tangentX);
            t = glm::normalize(t - v.normal * glm::dot(v.normal, t));
            v.tangent = glm::vec4(t, 1.0f);

            outVertices.push_back(v);
        }
    }

    // --- Index Builder ---
    for (int z = 0; z < resolution - 1; z++) {
        for (int x = 0; x < resolution - 1; x++) {
            uint32_t i0 = z * resolution + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = (z + 1) * resolution + x;
            uint32_t i3 = i2 + 1;

            outIndices.insert(outIndices.end(), { i0, i1, i2, i1, i3, i2 });
        }
    }

    // --- Skirt Generator ---
    const float skirtDepth = 20.0f;
    auto AddSkirtSegment = [&](uint32_t indexA, uint32_t indexB) {
        uint32_t skirtA = (uint32_t)outVertices.size();
        ModelVertex vA = outVertices[indexA]; vA.pos.y -= skirtDepth; outVertices.push_back(vA);

        uint32_t skirtB = (uint32_t)outVertices.size();
        ModelVertex vB = outVertices[indexB]; vB.pos.y -= skirtDepth; outVertices.push_back(vB);

        outIndices.insert(outIndices.end(), { indexA, skirtA, indexB, indexB, skirtA, skirtB });
        };

    for (int x = 0; x < resolution - 1; ++x) AddSkirtSegment(0 * resolution + x, 0 * resolution + (x + 1));
    for (int z = 0; z < resolution - 1; ++z) AddSkirtSegment(z * resolution + (resolution - 1), (z + 1) * resolution + (resolution - 1));
    for (int x = resolution - 1; x > 0; --x) AddSkirtSegment((resolution - 1) * resolution + x, (resolution - 1) * resolution + (x - 1));
    for (int z = resolution - 1; z > 0; --z) AddSkirtSegment(z * resolution + 0, (z - 1) * resolution + 0);

    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
        outVertices.clear(); outIndices.clear(); return;
    }

    // 🚀 FIX: Run Meshoptimizer AFTER both terrain AND skirts are fully built!
    meshopt_optimizeVertexCache(
        outIndices.data(),
        outIndices.data(),
        outIndices.size(),
        outVertices.size()
    );

    std::vector<ModelVertex> rearrangedVertices(outVertices.size());

    meshopt_optimizeVertexFetch(
        rearrangedVertices.data(),
        outIndices.data(),
        outIndices.size(),
        outVertices.data(),
        outVertices.size(),
        sizeof(ModelVertex)
    );

    outVertices = std::move(rearrangedVertices);
}