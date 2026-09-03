#pragma once

#include <vulkan/vulkan_core.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <array>
#include <mutex>
#include <atomic>

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
    uint32_t instanceOffset = 0;
    uint32_t indirectOffset = 0;
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
    float fadeStart;
    uint32_t totalInstances;
    uint32_t vertexCount;
    float fadeEnd;
    float _padding;

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

    void Cull(VkCommandBuffer commandBuffer, uint32_t currentFrameIndex);
    void Draw(VkCommandBuffer commandBuffer, VkDescriptorSet sharedDescriptorSet, uint32_t currentFrameIndex, uint32_t& outDrawCalls) const;

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

    uint32_t m_framesInFlight = 3;

    // --- SUBALLOCATOR GLOBALS ---
    uint32_t m_maxInstances = 5000000;
    uint32_t m_maxIndirect = 5000;

    VkBuffer m_instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_instanceMemory = VK_NULL_HANDLE;

    std::array<VkDescriptorSet, 3> m_globalComputeSets = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };

    std::array<VkBuffer, 3> m_culledBuffers = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    std::array<VkDeviceMemory, 3> m_culledMemories = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };

    std::array<VkBuffer, 3> m_indirectBuffers = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    std::array<VkDeviceMemory, 3> m_indirectMemories = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };

    struct FreeSpan { uint32_t offset; uint32_t count; };
    struct SpanReturn { FreeSpan instanceSpan; FreeSpan indirectSpan; };

    std::vector<FreeSpan> m_freeInstanceSpans;
    std::vector<FreeSpan> m_freeIndirectSpans;
    std::atomic<uint32_t> m_nextInstanceOffset{ 0 };
    std::atomic<uint32_t> m_nextIndirectOffset{ 0 };
    std::mutex m_allocMutex;
    DeferredQueue<SpanReturn> m_pendingSpanReturns;

    std::pair<uint32_t, uint32_t> AllocateSpace(uint32_t instanceCount, uint32_t indirectCount);
    void DeferSpanReturn(const FreeSpan& instSpan, const FreeSpan& indSpan, uint64_t safeFrame);
    // ----------------------------

    mutable std::vector<const GrassChunkGPU*> m_visibleChunksThisFrame;

    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    VkDescriptorPool m_computeDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_computeSetLayout = VK_NULL_HANDLE;
    VkPipeline m_computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE;

    std::unordered_map<int64_t, GrassChunkGPU> m_grassChunks;
    std::unordered_map<int64_t, std::shared_ptr<bool>> m_pendingFlags;
};