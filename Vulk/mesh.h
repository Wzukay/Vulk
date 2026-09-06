#pragma once

#define GLM_ENABLE_EXPERIMENTAL

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtc/packing.hpp>
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

inline uint32_t EncodeTangent(glm::vec4 t) {
    int8_t x = static_cast<int8_t>(std::round(std::clamp(t.x, -1.0f, 1.0f) * 127.0f));
    int8_t y = static_cast<int8_t>(std::round(std::clamp(t.y, -1.0f, 1.0f) * 127.0f));
    int8_t z = static_cast<int8_t>(std::round(std::clamp(t.z, -1.0f, 1.0f) * 127.0f));
    int8_t w = static_cast<int8_t>(std::round(std::clamp(t.w, -1.0f, 1.0f) * 127.0f));

    uint32_t packed = 0;
    packed |= (static_cast<uint32_t>(static_cast<uint8_t>(x)));
    packed |= (static_cast<uint32_t>(static_cast<uint8_t>(y)) << 8);
    packed |= (static_cast<uint32_t>(static_cast<uint8_t>(z)) << 16);
    packed |= (static_cast<uint32_t>(static_cast<uint8_t>(w)) << 24);
    return packed;
}

struct ModelVertex {
    glm::vec3 pos;              // 12 bytes
    uint32_t normal;            // 4 bytes
    uint32_t texCoord;          // 4 bytes (Packed R16G16_SFLOAT)
    uint32_t tangent;           // 4 bytes (Packed R8G8B8A8_SNORM)
    float coarseY;              // 4 bytes
    uint32_t coarseNormal;      // 4 bytes
    uint32_t coarseTangent;     // 4 bytes
    // Total: 36 bytes (Down from 76!)

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(ModelVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 4> getStaticAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 4> attrs{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ModelVertex, pos) };
        attrs[1] = { 1, 0, VK_FORMAT_R16G16_SNORM,     offsetof(ModelVertex, normal) };
        // Vulkan automatically unpacks these into vec2 and vec4!
        attrs[2] = { 2, 0, VK_FORMAT_R16G16_SFLOAT,    offsetof(ModelVertex, texCoord) };
        attrs[3] = { 3, 0, VK_FORMAT_R8G8B8A8_SNORM,   offsetof(ModelVertex, tangent) };
        return attrs;
    }

    static std::array<VkVertexInputAttributeDescription, 7> getTerrainAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 7> attrs{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ModelVertex, pos) };
        attrs[1] = { 1, 0, VK_FORMAT_R16G16_SNORM,     offsetof(ModelVertex, normal) };
        attrs[2] = { 2, 0, VK_FORMAT_R16G16_SFLOAT,    offsetof(ModelVertex, texCoord) };
        attrs[3] = { 3, 0, VK_FORMAT_R8G8B8A8_SNORM,   offsetof(ModelVertex, tangent) };
        // We keep the locations at 5, 6, 7 so you don't have to rewrite terrain.vert!
        attrs[4] = { 5, 0, VK_FORMAT_R32_SFLOAT,       offsetof(ModelVertex, coarseY) };
        attrs[5] = { 6, 0, VK_FORMAT_R16G16_SNORM,     offsetof(ModelVertex, coarseNormal) };
        attrs[6] = { 7, 0, VK_FORMAT_R8G8B8A8_SNORM,   offsetof(ModelVertex, coarseTangent) };
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
                (hash<uint32_t>()(vertex.texCoord) << 1);
            h ^= hash<uint32_t>()(vertex.tangent) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
}