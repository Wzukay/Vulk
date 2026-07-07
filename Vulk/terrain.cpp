#include "terrain.h"

void Terrain::GenerateChunk(int chunkX, int chunkZ, int resolution, float chunkSize, int seed,
    std::vector<ModelVertex>& outVertices, std::vector<uint32_t>& outIndices) {

    outVertices.clear();
    outVertices.reserve(resolution * resolution + (resolution - 1) * 8); // Buffer extra room for skirt vertices
    outIndices.clear();
    outIndices.reserve(((resolution - 1) * (resolution - 1) * 6) + ((resolution - 1) * 4 * 6)); // Buffer room for skirt indices

    TerrainNoiseLayers layers(seed);

    const float step = chunkSize / (float)(resolution - 1); // world units between adjacent verts

    float originX = chunkX * chunkSize;
    float originZ = chunkZ * chunkSize;

    int gridSize = resolution + 2;
    std::vector<float> heightGrid(gridSize * gridSize);

    for (int gz = 0; gz < gridSize; ++gz) {
        float worldZ = originZ + (gz - 1) * step;
        for (int gx = 0; gx < gridSize; ++gx) {
            float worldX = originX + (gx - 1) * step;
            heightGrid[gz * gridSize + gx] = SampleHeight(layers, worldX, worldZ);
        }
    }

    auto GetCachedHeight = [&](int localX, int localZ) -> float {
        return heightGrid[(localZ + 1) * gridSize + (localX + 1)];
        };

    // 1. Vertices, sampled at WORLD coordinates so edges align across chunks
    for (int z = 0; z < resolution; z++) {
        float worldZ = originZ + z * step;
        for (int x = 0; x < resolution; x++) {
            float worldX = originX + x * step;

            // O(1) Cache lookups replacing 5 separate recursive FBM iterations
            float h = GetCachedHeight(x, z);
            float hL = GetCachedHeight(x - 1, z);
            float hR = GetCachedHeight(x + 1, z);
            float hD = GetCachedHeight(x, z - 1);
            float hU = GetCachedHeight(x, z + 1);

            ModelVertex v;
            v.pos = glm::vec3(worldX, h, worldZ);
            v.texCoord = glm::vec2(worldX * 0.1f, worldZ * 0.1f); // Seamlessly tiling world-space textures

            glm::vec3 tangentX(2.0f * step, hR - hL, 0.0f);
            glm::vec3 tangentZ(0.0f, hU - hD, 2.0f * step);
            v.normal = glm::normalize(glm::cross(tangentZ, tangentX));

            // ANALYTICAL TANGENTS
            // Gram-Schmidt orthogonalization calculates tangents directly,
            // skipping thousands of CPU loop iterations entirely.
            glm::vec3 t = glm::normalize(tangentX);
            t = glm::normalize(t - v.normal * glm::dot(v.normal, t));
            v.tangent = glm::vec4(t, 1.0f);

            outVertices.push_back(v);
        }
    }

    // 2. Indices
    for (int z = 0; z < resolution - 1; z++) {
        for (int x = 0; x < resolution - 1; x++) {
            uint32_t i0 = z * resolution + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = (z + 1) * resolution + x;
            uint32_t i3 = i2 + 1;

            // Counter-Clockwise (CCW) polygon layout matching Vulkan graphics pipeline configurations
            outIndices.insert(outIndices.end(), { i0, i1, i2, i1, i3, i2 });
        }
    }

    const float skirtDepth = 20.0f; // Distance to pull the edge wall down (bumped for the larger height range)

    auto AddSkirtSegment = [&](uint32_t indexA, uint32_t indexB) {
        // Duplicate Vertex A and push its elevation downward
        uint32_t skirtA = (uint32_t)outVertices.size();
        ModelVertex vA = outVertices[indexA];
        vA.pos.y -= skirtDepth;
        outVertices.push_back(vA);

        // Duplicate Vertex B and push its elevation downward
        uint32_t skirtB = (uint32_t)outVertices.size();
        ModelVertex vB = outVertices[indexB];
        vB.pos.y -= skirtDepth;
        outVertices.push_back(vB);

        // Bind the geometry together into two solid outward-facing triangles
        outIndices.push_back(indexA);
        outIndices.push_back(skirtA);
        outIndices.push_back(indexB);

        outIndices.push_back(indexB);
        outIndices.push_back(skirtA);
        outIndices.push_back(skirtB);
        };

    // A. South Boundary Edge (z = 0) -> West to East
    for (int x = 0; x < resolution - 1; ++x) {
        uint32_t idxA = 0 * resolution + x;
        uint32_t idxB = 0 * resolution + (x + 1);
        AddSkirtSegment(idxA, idxB);
    }

    // B. East Boundary Edge (x = resolution - 1) -> South to North
    for (int z = 0; z < resolution - 1; ++z) {
        uint32_t idxA = z * resolution + (resolution - 1);
        uint32_t idxB = (z + 1) * resolution + (resolution - 1);
        AddSkirtSegment(idxA, idxB);
    }

    // C. North Boundary Edge (z = resolution - 1) -> East to West
    for (int x = resolution - 1; x > 0; --x) {
        uint32_t idxA = (resolution - 1) * resolution + x;
        uint32_t idxB = (resolution - 1) * resolution + (x - 1);
        AddSkirtSegment(idxA, idxB);
    }

    // D. West Boundary Edge (x = 0) -> North to South
    for (int z = resolution - 1; z > 0; --z) {
        uint32_t idxA = z * resolution + 0;
        uint32_t idxB = (z - 1) * resolution + 0;
        AddSkirtSegment(idxA, idxB);
    }
}