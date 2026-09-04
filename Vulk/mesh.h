#pragma once

#define GLM_ENABLE_EXPERIMENTAL

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <functional>
#include <algorithm>
#include <limits>

inline glm::vec2 OctWrap(glm::vec2 v) {
    return (1.0f - glm::abs(glm::vec2(v.y, v.x))) *
        glm::vec2(v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f);
}

// Packs a 3D normal into a single 32-bit unsigned integer (4 bytes)
inline uint32_t EncodeNormal(glm::vec3 n) {
    n /= (std::abs(n.x) + std::abs(n.y) + std::abs(n.z));
    glm::vec2 p(n.x, n.z);
    p = n.y <= 0.0f ? OctWrap(p) : p;

    // Map float [-1.0, 1.0] to 16-bit signed integer [-32767, 32767]
    int16_t x = static_cast<int16_t>(std::round(std::clamp(p.x, -1.0f, 1.0f) * 32767.0f));
    int16_t y = static_cast<int16_t>(std::round(std::clamp(p.y, -1.0f, 1.0f) * 32767.0f));

    // Pack into a single uint32_t (R16G16 format)
    uint32_t packed = 0;
    packed |= (static_cast<uint16_t>(x) & 0xFFFF);
    packed |= (static_cast<uint32_t>(static_cast<uint16_t>(y) & 0xFFFF) << 16);

    return packed;
}

inline glm::vec3 DecodeNormal(uint32_t packed) {
    float fx = static_cast<int16_t>(packed & 0xFFFF) / 32767.0f;
    float fy = static_cast<int16_t>((packed >> 16) & 0xFFFF) / 32767.0f;

    glm::vec3 n(fx, 1.0f - std::abs(fx) - std::abs(fy), fy);
    float t = std::max(-n.y, 0.0f);
    n.x += n.x >= 0.0f ? -t : t;
    n.z += n.z >= 0.0f ? -t : t;

    return glm::normalize(n);
}

struct ModelVertex {
    glm::vec3 pos;
    uint32_t normal;
    glm::vec2 texCoord;
    glm::vec4 tangent;
    glm::vec3 color;
    float coarseY;
    uint32_t coarseNormal;
    glm::vec4 coarseTangent;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(ModelVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    // Static & Instanced meshes only care about locations 0 to 4
    static std::array<VkVertexInputAttributeDescription, 5> getStaticAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 5> attrs{};

        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ModelVertex, pos) };
        attrs[1] = { 1, 0, VK_FORMAT_R16G16_SNORM,     offsetof(ModelVertex, normal) };
        attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(ModelVertex, texCoord) };
        attrs[3] = { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ModelVertex, tangent) };
        attrs[4] = { 4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ModelVertex, color) };

        return attrs;
    }

    // Terrain consumes all 8 attributes (locations 0 to 7)
    static std::array<VkVertexInputAttributeDescription, 8> getTerrainAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 8> attrs{};

        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ModelVertex, pos) };
        attrs[1] = { 1, 0, VK_FORMAT_R16G16_SNORM,     offsetof(ModelVertex, normal) };
        attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(ModelVertex, texCoord) };
        attrs[3] = { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ModelVertex, tangent) };
        attrs[4] = { 4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ModelVertex, color) };
        attrs[5] = { 5, 0, VK_FORMAT_R32_SFLOAT,       offsetof(ModelVertex, coarseY) };
        attrs[6] = { 6, 0, VK_FORMAT_R16G16_SNORM,     offsetof(ModelVertex, coarseNormal) };
        attrs[7] = { 7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ModelVertex, coarseTangent) };

        return attrs;
    }
};

struct RenderMesh {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
    uint32_t vertexCount = 0;

    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;

    // Inlined so mesh.cpp can be deleted
    inline void CleanUp(VkDevice device) {
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
};

namespace std {
    template<> struct hash<ModelVertex> {
        size_t operator()(ModelVertex const& vertex) const {
            size_t h = ((hash<glm::vec3>()(vertex.pos) ^
                (hash<uint32_t>()(vertex.normal) << 1)) >> 1) ^
                (hash<glm::vec2>()(vertex.texCoord) << 1);
            h ^= hash<float>()(vertex.tangent.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= hash<glm::vec3>()(vertex.color) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
}