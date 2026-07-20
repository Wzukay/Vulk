#pragma once

#define GLM_ENABLE_EXPERIMENTAL   // <-- MUST be first

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <functional>
#include <algorithm>
#include <limits>

struct ModelVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 tangent;
    glm::vec3 color;
    glm::vec3 coarsePos;
    glm::vec3 coarseNormal;

    bool operator==(const ModelVertex& other) const {
        return pos == other.pos && normal == other.normal &&
            texCoord == other.texCoord && tangent == other.tangent &&
            color == other.color;
    }

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(ModelVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 7> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 7> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(ModelVertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(ModelVertex, normal);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(ModelVertex, texCoord);

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(ModelVertex, tangent);

        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 4; // Matches layout(location = 4) in your GLSL Vertex Shader
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32_SFLOAT; // RGB is 3 floats
        attributeDescriptions[4].offset = offsetof(ModelVertex, color);

        attributeDescriptions[5].binding = 0;
        attributeDescriptions[5].location = 5;
        attributeDescriptions[5].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[5].offset = offsetof(ModelVertex, coarsePos);

        attributeDescriptions[6].binding = 0;
        attributeDescriptions[6].location = 6;
        attributeDescriptions[6].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[6].offset = offsetof(ModelVertex, coarseNormal);

        return attributeDescriptions;
    }
};

struct InstanceData {
    glm::mat4 modelMatrix;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription desc{};
        desc.binding = 1; // Binding 1 is reserved for per-instance data
        desc.stride = sizeof(InstanceData);
        desc.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        return desc;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> descs(4);
        // A mat4 takes up 4 attribute locations (vec4s) in the shader
        for (int i = 0; i < 4; i++) {
            descs[i].binding = 1;
            descs[i].location = 7 + i;
            descs[i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            descs[i].offset = sizeof(glm::vec4) * i;
        }
        return descs;
    }
};

struct RenderMesh {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
    uint32_t vertexCount = 0;

    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;

    void CleanUp(VkDevice device);
};

namespace std {
    template<> struct hash<ModelVertex> {
        size_t operator()(ModelVertex const& vertex) const {
            size_t h = ((hash<glm::vec3>()(vertex.pos) ^
                (hash<glm::vec3>()(vertex.normal) << 1)) >> 1) ^
                (hash<glm::vec2>()(vertex.texCoord) << 1);
            h ^= hash<float>()(vertex.tangent.x) + 0x9e3779b9 + (h << 6) + (h >> 2);

            h ^= hash<glm::vec3>()(vertex.color) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
}

// ---------------

struct WaterVertex {
    glm::vec3 pos;
    glm::vec2 uv;
};

struct WaterMesh {
    std::vector<WaterVertex> vertices;
    std::vector<uint32_t> indices;
    float waterHeight = 0.0f;
};

class WaterMeshGen {
public:
    static WaterMesh GenerateLake(const std::vector<glm::vec2>& footprintXZ, float waterHeight, float gridSpacing);
    static WaterMesh GenerateRiver(const std::vector<glm::vec3>& centerline, float width, int segmentsPerPoint = 1);

    // Builds a lake mesh directly from a boolean "is this cell part of the
    // lake" mask on a regular grid, instead of approximating the basin's
    // footprint with a convex hull. A hull inflates concave/irregular basin
    // shapes into a convex blob, which makes the water spill out over
    // higher ground at the edges instead of hugging the actual depression.
    // `cellMask` is width*height, row-major, true where that grid cell
    // belongs to the lake. `originX/originZ` is the world position of
    // mask cell (0,0), and `cellSize` is the spacing between mask cells.
    static WaterMesh GenerateLakeFromMask(
        const std::vector<bool>& cellMask,
        int width, int height,
        float originX, float originZ, float cellSize,
        float waterHeight);

private:
    static bool PointInPolygon(const glm::vec2& p, const std::vector<glm::vec2>& poly);
};