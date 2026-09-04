#pragma once

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_set>
#include <optional>

struct UniformBufferObject {
    alignas(16) glm::mat4 view;              // 64 bytes
    alignas(16) glm::mat4 proj;              // 64 bytes

    // Chunk 1: 16 bytes
    alignas(16) glm::vec3 cameraPos;         // 12 bytes
    float ambient;                           // 4 bytes

    // Chunk 2: 16 bytes
    alignas(16) glm::vec4 fadeParams;        // 16 bytes

    // Chunk 3: 16 bytes
    alignas(8)  glm::vec2 screenSize;        // 8 bytes
    float specularPower;                     // 4 bytes
    uint32_t lightCount;                     // 4 bytes

    // Chunk 4: 16 bytes (Explicitly padded to prevent C++ from crushing the struct)
    float fogStart;                          // 4 bytes
    float fogEnd;                            // 4 bytes
    float _pad2[2];                          // 8 bytes of padding

    // Sun Data for Procedural Sky
    alignas(16) glm::vec4 sunDirection;
    alignas(16) glm::vec4 sunColor;

    // Matrices (64 bytes each, alignas(16) guarantees std140 matrix boundaries)
    alignas(16) glm::mat4 inverseViewProj;
    alignas(16) glm::mat4 inverseProj;
    alignas(16) glm::mat4 inverseView;
};
struct SSAOUBO {
    glm::mat4 projection;
    glm::mat4 inverseProjection;
    glm::vec4 samples[24];
    glm::vec4 noise[16];
};

struct PushConstants {
    glm::mat4 modelMatrix;      // 64 bytes
    uint32_t textureId;         // 4 bytes
    uint32_t normalTextureId;   // 4 bytes
    uint32_t objectId;          // 4 bytes
    float morphBlend;
};

struct SSAOPushConstants {
    glm::vec2 screenSize;
    float radius;
    float bias;
    glm::vec2 renderScale;
};
struct SSAOBlurPushConstants {
    glm::vec2 screenSize;
    glm::vec2 blurDirection;
    float colorSigma;
    float spatialSigma;
    glm::vec2 renderScale;
};
struct FSRConstants {
    glm::vec4 const0;
    glm::vec4 const1;
    glm::vec4 const2;
    glm::vec4 const3;
    float sharpness;
    uint32_t enableSSAO;
    glm::vec2 renderScale;
};

struct RenderTarget {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE; // leave VK_NULL_HANDLE if this target has no dedicated sampler

    void Destroy(VkDevice device) {
        if (device == VK_NULL_HANDLE) return;
        if (sampler != VK_NULL_HANDLE) { vkDestroySampler(device, sampler, nullptr); sampler = VK_NULL_HANDLE; }
        if (view != VK_NULL_HANDLE) { vkDestroyImageView(device, view, nullptr);  view = VK_NULL_HANDLE; }
        if (image != VK_NULL_HANDLE) { vkDestroyImage(device, image, nullptr);     image = VK_NULL_HANDLE; }
        if (memory != VK_NULL_HANDLE) { vkFreeMemory(device, memory, nullptr);      memory = VK_NULL_HANDLE; }
    }
};
struct DrawEntry {
    uint32_t objectIndex;
    uint32_t subMeshIndex; // index into globalSubMeshes
    float distSq;
    int64_t chunkKey = -1;
    float cachedMaxScale = 1.0f;
};
struct IndirectCommand {
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t firstVertex;
    uint32_t firstInstance;
};

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    const bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities = {};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct FrustumPlane {
    glm::vec3 normal;
    float distance;
};

struct FreeSpan { uint32_t offset; uint32_t count; };
struct SpanReturn { FreeSpan vertexSpan; FreeSpan indexSpan; };
struct TerrainChunkGPU {
    int64_t key = 0;
    glm::vec3 center{ 0.0f };
    float radius = 0.0f;
    int lod = 0;
    bool ready = false;

    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    bool cachedOccluded = false;
};
struct TerrainChunkGPUData {
    glm::vec4 bounds;
    uint32_t indexCount;
    uint32_t indexOffset;
    uint32_t vertexOffset;
    uint32_t lod;
};

struct ComputePush {
    glm::mat4 viewProj;         // 64 bytes
    glm::vec4 fadeParams;       // 16 bytes
    glm::vec3 cameraPos;        // 12 bytes
    float chunkSize;            // 4 bytes
    glm::vec2 hzbSize;          // 8 bytes
    float maxMip;               // 4 bytes
    float transitionWidth;      // 4 bytes
    uint32_t totalChunks;       // 4 bytes
    // Total: 116 bytes
};
static_assert(sizeof(ComputePush) == 116, "ComputePush size must be 116 bytes");

struct GrassChunkMetadata {
    glm::vec3 center;
    float radius;
    uint32_t firstInstance;
    uint32_t instanceCount;
    uint32_t pad[2]; // align to 16 bytes
};

struct PropInstance {
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
    float customPayload;

    std::vector<std::string> lodMeshes;
};
struct InstanceData {
    alignas(16) glm::mat4 modelMatrix;
    alignas(16) glm::vec4 customData;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 1;
        bindingDescription.stride = sizeof(InstanceData);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 5> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{};

        // Locations 5, 6, 7, 8 for mat4 columns
        for (int i = 0; i < 4; i++) {
            attributeDescriptions[i].binding = 1;
            attributeDescriptions[i].location = 5 + i;
            attributeDescriptions[i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributeDescriptions[i].offset = offsetof(InstanceData, modelMatrix) + sizeof(glm::vec4) * i;
        }

        // Location 9 for customData
        attributeDescriptions[4].binding = 1;
        attributeDescriptions[4].location = 9;
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[4].offset = offsetof(InstanceData, customData);

        return attributeDescriptions;
    }
};