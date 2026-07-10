#include "mesh.h"

void RenderMesh::CleanUp(VkDevice device) {
    {
        if (indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, indexBuffer, nullptr);
            indexBuffer = VK_NULL_HANDLE;
        }
        if (indexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, indexBufferMemory, nullptr);
            indexBufferMemory = VK_NULL_HANDLE;
        }
        if (vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, vertexBuffer, nullptr);
            vertexBuffer = VK_NULL_HANDLE;
        }
        if (vertexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, vertexBufferMemory, nullptr);
            vertexBufferMemory = VK_NULL_HANDLE;
        }
        indexCount = 0;
        vertexCount = 0;
    }
}

bool WaterMeshGen::PointInPolygon(const glm::vec2& p, const std::vector<glm::vec2>& poly) {
    bool inside = false;
    size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const glm::vec2& a = poly[i];
        const glm::vec2& b = poly[j];
        bool intersects = ((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x);
        if (intersects) inside = !inside;
    }
    return inside;
}

WaterMesh WaterMeshGen::GenerateLake(const std::vector<glm::vec2>& footprintXZ, float waterHeight, float gridSpacing) {
    WaterMesh mesh;
    mesh.waterHeight = waterHeight;

    if (footprintXZ.size() < 3 || gridSpacing <= 0.0f) return mesh;

    glm::vec2 minB(std::numeric_limits<float>::max());
    glm::vec2 maxB(std::numeric_limits<float>::lowest());
    for (const auto& p : footprintXZ) {
        minB = glm::min(minB, p);
        maxB = glm::max(maxB, p);
    }

    int cellsX = std::max(1, (int)std::ceil((maxB.x - minB.x) / gridSpacing));
    int cellsZ = std::max(1, (int)std::ceil((maxB.y - minB.y) / gridSpacing));
    int vertsX = cellsX + 1;
    int vertsZ = cellsZ + 1;

    // Build full grid of vertices over the bounding box.
    std::vector<int> vertIndexLookup(vertsX * vertsZ, -1); // -1 = not used (outside polygon)
    mesh.vertices.reserve(vertsX * vertsZ);

    for (int z = 0; z < vertsZ; ++z) {
        for (int x = 0; x < vertsX; ++x) {
            glm::vec2 worldXZ = minB + glm::vec2(x * gridSpacing, z * gridSpacing);

            WaterVertex v{};
            v.pos = glm::vec3(worldXZ.x, waterHeight, worldXZ.y);
            v.uv = glm::vec2((float)x / cellsX, (float)z / cellsZ);

            int idx = z * vertsX + x;
            vertIndexLookup[idx] = (int)mesh.vertices.size();
            mesh.vertices.push_back(v);
        }
    }

    // Emit a quad (2 tris) only if its center falls inside the lake polygon.
    for (int z = 0; z < cellsZ; ++z) {
        for (int x = 0; x < cellsX; ++x) {
            glm::vec2 cellCenter = minB + glm::vec2((x + 0.5f) * gridSpacing, (z + 0.5f) * gridSpacing);
            if (!PointInPolygon(cellCenter, footprintXZ)) continue;

            uint32_t i00 = vertIndexLookup[z * vertsX + x];
            uint32_t i10 = vertIndexLookup[z * vertsX + (x + 1)];
            uint32_t i01 = vertIndexLookup[(z + 1) * vertsX + x];
            uint32_t i11 = vertIndexLookup[(z + 1) * vertsX + (x + 1)];

            mesh.indices.push_back(i00);
            mesh.indices.push_back(i10);
            mesh.indices.push_back(i11);

            mesh.indices.push_back(i00);
            mesh.indices.push_back(i11);
            mesh.indices.push_back(i01);
        }
    }

    return mesh;
}

WaterMesh WaterMeshGen::GenerateLakeFromMask(
    const std::vector<bool>& cellMask,
    int width, int height,
    float originX, float originZ, float cellSize,
    float waterHeight)
{
    WaterMesh mesh;
    mesh.waterHeight = waterHeight;

    if (width <= 0 || height <= 0 || cellSize <= 0.0f) return mesh;
    if ((int)cellMask.size() != width * height) return mesh;

    int vertsX = width + 1;
    int vertsZ = height + 1;

    // Shared corner vertices, only allocated where at least one adjacent
    // lake cell touches them, so the mesh follows the mask's exact outline
    // (no convex-hull inflation over dry ground).
    std::vector<int> vertIndexLookup(vertsX * vertsZ, -1);

    auto cellIsLake = [&](int cx, int cz) -> bool {
        if (cx < 0 || cx >= width || cz < 0 || cz >= height) return false;
        return cellMask[cz * width + cx];
        };

    auto getOrAddVertex = [&](int vx, int vz) -> uint32_t {
        int idx = vz * vertsX + vx;
        if (vertIndexLookup[idx] != -1) return (uint32_t)vertIndexLookup[idx];

        WaterVertex v{};
        v.pos = glm::vec3(originX + vx * cellSize, waterHeight, originZ + vz * cellSize);
        v.uv = glm::vec2((float)vx / (float)width, (float)vz / (float)height);

        vertIndexLookup[idx] = (int)mesh.vertices.size();
        mesh.vertices.push_back(v);
        return (uint32_t)vertIndexLookup[idx];
        };

    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            if (!cellIsLake(x, z)) continue;

            uint32_t i00 = getOrAddVertex(x, z);
            uint32_t i10 = getOrAddVertex(x + 1, z);
            uint32_t i01 = getOrAddVertex(x, z + 1);
            uint32_t i11 = getOrAddVertex(x + 1, z + 1);

            mesh.indices.push_back(i00);
            mesh.indices.push_back(i10);
            mesh.indices.push_back(i11);

            mesh.indices.push_back(i00);
            mesh.indices.push_back(i11);
            mesh.indices.push_back(i01);
        }
    }

    return mesh;
}

WaterMesh WaterMeshGen::GenerateRiver(const std::vector<glm::vec3>& centerline, float width, int segmentsPerPoint) {
    WaterMesh mesh;
    if (centerline.size() < 2 || width <= 0.0f) return mesh;

    // Build a subdivided centerline (simple linear interpolation between input points).
    std::vector<glm::vec3> path;
    segmentsPerPoint = std::max(1, segmentsPerPoint);
    for (size_t i = 0; i + 1 < centerline.size(); ++i) {
        for (int s = 0; s < segmentsPerPoint; ++s) {
            float t = (float)s / (float)segmentsPerPoint;
            path.push_back(glm::mix(centerline[i], centerline[i + 1], t));
        }
    }
    path.push_back(centerline.back());

    mesh.waterHeight = centerline.front().y; // used as a fallback/reference height

    float totalLength = 0.0f;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        totalLength += glm::length(path[i + 1] - path[i]);
    }
    if (totalLength <= 0.0f) return mesh;

    float accumulatedLength = 0.0f;
    mesh.vertices.reserve(path.size() * 2);

    for (size_t i = 0; i < path.size(); ++i) {
        // Tangent: average of incoming/outgoing segment directions for a smoother ribbon.
        glm::vec3 tangent;
        if (i == 0) {
            tangent = glm::normalize(path[i + 1] - path[i]);
        }
        else if (i == path.size() - 1) {
            tangent = glm::normalize(path[i] - path[i - 1]);
        }
        else {
            tangent = glm::normalize((path[i + 1] - path[i - 1]));
        }

        glm::vec3 right = glm::normalize(glm::cross(tangent, glm::vec3(0.0f, 1.0f, 0.0f)));

        if (i > 0) accumulatedLength += glm::length(path[i] - path[i - 1]);
        float vCoord = accumulatedLength / totalLength;

        WaterVertex left{};
        left.pos = path[i] - right * (width * 0.5f);
        left.uv = glm::vec2(0.0f, vCoord * (totalLength / width)); // tile UV by width so scroll speed reads consistently

        WaterVertex rightV{};
        rightV.pos = path[i] + right * (width * 0.5f);
        rightV.uv = glm::vec2(1.0f, vCoord * (totalLength / width));

        mesh.vertices.push_back(left);
        mesh.vertices.push_back(rightV);
    }

    for (size_t i = 0; i + 1 < path.size(); ++i) {
        uint32_t i0 = (uint32_t)(i * 2);
        uint32_t i1 = i0 + 1;
        uint32_t i2 = i0 + 2;
        uint32_t i3 = i0 + 3;

        mesh.indices.push_back(i0);
        mesh.indices.push_back(i2);
        mesh.indices.push_back(i1);

        mesh.indices.push_back(i1);
        mesh.indices.push_back(i2);
        mesh.indices.push_back(i3);
    }

    return mesh;
}