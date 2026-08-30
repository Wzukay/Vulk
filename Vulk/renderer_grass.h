#pragma once

#include <vulkan/vulkan_core.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include "gpu_async.h"
#include "ring_buffer_uploader.h"

class VulkanRenderer;

struct GrassInstance {
    glm::vec3 position;
    float rotation;
    glm::vec3 scale;
    float windOffset;
};

struct GrassChunkGPU {
    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory instanceMemory = VK_NULL_HANDLE;

    // NEW: Compute Shader Output Buffers
    VkBuffer culledBuffer = VK_NULL_HANDLE;
    VkDeviceMemory culledMemory = VK_NULL_HANDLE;

    VkBuffer indirectBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indirectMemory = VK_NULL_HANDLE;

    VkDescriptorSet computeDescriptorSet = VK_NULL_HANDLE;

    uint32_t instanceCount = 0;
    glm::vec3 center = glm::vec3(0.0f);
    float radius = 0.0f;
};

struct GrassPushConstants {
    float time;
    uint32_t textureId;
    float windStrength;
    float windSpeed;
    float lodFactor;
};

struct GrassComputePushConstants {
    glm::vec3 cameraPos;
    float maxDist;
    uint32_t totalInstances;
    uint32_t vertexCount;

    glm::vec2 _padding;

    glm::vec4 frustumPlanes[6];
};

class GrassRenderer {
public:
    void Init(VkDevice device, VulkanRenderer* renderer,
        RingBufferUploader* uploader,
        VkFormat colorFormat, VkFormat depthFormat,
        VkDescriptorSetLayout sharedSetLayout,
        VkSampleCountFlagBits msaaSamples);
    void Cleanup();

    void AddGrass(int64_t key, const std::vector<GrassInstance>& grassInstances);
    void Tick(uint64_t currentFrame);

    void Cull(VkCommandBuffer commandBuffer); // NEW: Compute step
    void Draw(VkCommandBuffer commandBuffer, VkDescriptorSet sharedDescriptorSet, uint32_t& outDrawCalls) const;

    bool HasChunk(int64_t key) const;
    void RemoveChunk(int64_t key);
    void CancelPendingUpload(int64_t key);

    void CreatePipeline(VkFormat colorFormat, VkFormat depthFormat,
        VkDescriptorSetLayout sharedSetLayout,
        VkSampleCountFlagBits msaaSamples);

    void CreateComputePipeline();

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VulkanRenderer* m_renderer = nullptr;
    RingBufferUploader* m_uploader = nullptr;

    // NEW: List to pass visible chunks from Cull to Draw
    mutable std::vector<const GrassChunkGPU*> m_visibleChunksThisFrame;

    // Graphics Pipeline
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    // Compute Pipeline Resources
    VkDescriptorPool m_computeDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_computeSetLayout = VK_NULL_HANDLE;
    VkPipeline m_computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE;

    std::unordered_map<int64_t, GrassChunkGPU> m_grassChunks;
    std::unordered_map<int64_t, std::shared_ptr<bool>> m_pendingFlags;
};