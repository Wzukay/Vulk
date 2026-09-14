#pragma once

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_set>
#include <optional>
#include <string>
#include <array>

#include "mesh_types.h"

struct UniformBufferObject {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;

    alignas(16) glm::vec3 cameraPos;
    float ambient;

    alignas(16) glm::vec4 fadeParams;

    alignas(8)  glm::vec2 screenSize;
    float specularPower;
    uint32_t lightCount;

    float fogStart;
    float fogEnd;
    float _pad2[2];

    alignas(16) glm::vec4 sunDirection;
    alignas(16) glm::vec4 sunColor;

    alignas(16) glm::mat4 inverseViewProj;
    alignas(16) glm::mat4 inverseProj;
    alignas(16) glm::mat4 inverseView;

    alignas(16) glm::mat4 previousViewProj;
};

struct SSAOUBO {
    glm::mat4 projection;
    glm::mat4 inverseProjection;
    glm::vec4 samples[24];
    glm::vec4 noise[16];
};

struct PushConstants {
    glm::mat4 modelMatrix;
    uint32_t textureId;
    uint32_t normalTextureId;
    uint32_t objectId;
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
    uint32_t enableMotionBlur;
    uint32_t enableGodRays;
    glm::vec2 lightScreenPos;
    glm::vec4 lightColorAndIntensity;
};

struct RenderTarget {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

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
    uint32_t subMeshIndex;
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
struct TerrainCullPush {
    glm::mat4 viewProj;
    glm::vec4 fadeParams;
    glm::vec3 cameraPos;
    float chunkSize;
    glm::vec2 hzbSize;
    float maxMip;
    float transitionWidth;
    uint32_t totalChunks;
};

struct GrassChunkMetadata {
    glm::vec3 center;
    float radius;
    uint32_t firstInstance;
    uint32_t instanceCount;
    uint32_t pad[2];
};

struct PropInstance {
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
    float customPayload;

    std::string lodGroupName;
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

        for (int i = 0; i < 4; i++) {
            attributeDescriptions[i].binding = 1;
            attributeDescriptions[i].location = 5 + i;
            attributeDescriptions[i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributeDescriptions[i].offset = offsetof(InstanceData, modelMatrix) + sizeof(glm::vec4) * i;
        }

        attributeDescriptions[4].binding = 1;
        attributeDescriptions[4].location = 9;
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[4].offset = offsetof(InstanceData, customData);

        return attributeDescriptions;
    }
};

struct WaterChunk {
    glm::vec3 center{ 0.0f };
    float radius = 0.0f;

    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
};
struct WaterPushConstants {
    glm::vec3 cameraPos;
    float time;
    glm::vec2 renderSize;
    glm::vec2 sceneUvScale;
};
struct WaterCullPush {
    glm::mat4 viewProj{ 1.0f };
    glm::vec3 cameraPos{ 0.0f };
    float maxDistance = 0.0f;
    uint32_t chunkCount = 0;
    glm::vec3 padding{ 0.0f };
};
struct WaterChunkGPUData {
    glm::vec4 centerRadius{ 0.0f };
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t padding = 0;
};

struct StaticCullPush {
    glm::mat4 viewProj;
    glm::vec3 cameraPos;
    uint32_t totalInstances;
    float maxDistance;
    float _padding;
    glm::vec2 hzbSize;
    glm::vec4 axisLengths;
};
struct MeshBufferAllocation {
    uint32_t firstIndex;
    int32_t vertexOffset;
    std::vector<SubMesh> subMeshes;
    float maxBoundingRadius;
    uint32_t lodCount;
};
struct alignas(16) StaticInstanceCullData {
    glm::mat4 modelMatrix;
    glm::vec4 worldPositionRadius;
    glm::uvec4 drawData;
};
struct StaticIndirectBatch {
    VkDrawIndexedIndirectCommand command{};
    uint32_t outputBase = 0;
    uint32_t sourceCount = 0;
    uint32_t textureId = 0;
    uint32_t normalTextureId = 0;
};

struct ClusterAABB {
    glm::vec4 minPoint;
    glm::vec4 maxPoint;
};
struct ClusterRecord {
    uint32_t offset;
    uint32_t count;
};

struct BoidBehavior {
    float separationRadius = 12.0f;
    float alignmentRadius = 0.0f;
    float cohesionRadius = 20.0f;
    float maxSpeed = 15.0f;
    float minSpeed = 6.0f;
    float turnSpeed = 5.0f;
    float wanderStrength = 2.5f;
    float driftSpeed = 0.8f;
    float driftRadius = 150.0f;
    uint32_t animationType = 0;
    uint32_t textureId = 0;
    glm::vec4 color1 = glm::vec4(1.0f);
    glm::vec4 color2 = glm::vec4(1.0f);
};
struct BoidInstance {
    alignas(16) glm::vec4 position;
    alignas(16) glm::vec4 velocity;
};
struct SwarmData {
    std::vector<BoidInstance> instances;
    BoidBehavior behavior;
};
struct alignas(16) BoidComputeParams {
    float deltaTime;
    uint32_t boidCount;
    float separationRadius;
    float alignmentRadius;

    float cohesionRadius;
    float maxSpeed;
    float minSpeed;
    float turnSpeed;

    glm::vec4 centerAndRadius;

    float wanderStrength;
    uint32_t padding[3];
};
struct BoidDrawPushConstants {
    uint32_t textureId;
    uint32_t animationType;
    float pad[2];
    glm::vec4 color1;
    glm::vec4 color2;
};

struct SkyParams {
    glm::vec3 zenithColor;
    glm::vec3 horizonColor;
    float starFade;
    float cloudTime;
    float coverage;
};
struct SkyboxPushConstants {
    float time;
    float timeScale;
    float starFade;
    float coverage;
    glm::vec4 zenithColor;
    glm::vec4 horizonColor;
};