#pragma once
#include <vulkan/vulkan_core.h>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <glm/glm.hpp>
#include "gpu_instances.h"

class VulkanRenderer;
class RingBufferUploader;

static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

struct BoidGarbage {
    std::vector<VkDescriptorSet> sets;
    uint64_t safeFrame;
};

struct BoidSwarmGPU {
    uint32_t boidOffset = 0;
    uint32_t boidCount = 0;
    BoidBehavior behavior;
    int pingPongIndex = 0;
    VkDescriptorSet computeSets[MAX_FRAMES_IN_FLIGHT] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    glm::vec3 baseCenter = glm::vec3(0.0f);
    glm::vec3 currentCenter = glm::vec3(0.0f);
    float lifeTime = 0.0f;
};

class BoidRenderer {
public:
    void Init(VkDevice device, VulkanRenderer* renderer,
        RingBufferUploader* uploader,
        VkFormat colorFormat, VkFormat depthFormat,
        VkDescriptorSetLayout sharedSetLayout,
        VkSampleCountFlagBits msaaSamples);
    void Cleanup();

    void AddSwarms(int64_t chunkKey, const std::vector<SwarmData>& swarms);
    void RemoveSwarms(int64_t chunkKey);
    void TickCompute(VkCommandBuffer computeCmd, float deltaTime);
    void Draw(VkCommandBuffer drawCmd, VkDescriptorSet sharedDescriptorSet, uint32_t& outDrawCalls);

    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

private:
    std::unordered_map<int64_t, std::vector<BoidSwarmGPU>> m_swarms;

    RingBufferUploader* m_uploader = nullptr;
    std::unordered_map<int64_t, std::shared_ptr<std::atomic_bool>> m_pendingUploads;

    std::vector<BoidGarbage> m_garbageSets;

    struct FreeSpan { uint32_t offset; uint32_t count; };
    std::vector<FreeSpan> m_freeBoidSpans;
    std::atomic<uint32_t> m_nextBoidOffset{ 0 };
    std::mutex m_allocMutex;

    uint32_t AllocateSpace(uint32_t boidCount);

    uint32_t m_framesInFlight = 3;
    uint32_t m_maxBoids = 100000;

    VkBuffer m_boidBuffers[MAX_FRAMES_IN_FLIGHT] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory m_boidMemories[MAX_FRAMES_IN_FLIGHT] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };

    VkDevice m_device = VK_NULL_HANDLE;
    VulkanRenderer* m_renderer = nullptr;

    VkPipeline m_computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_computeSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    VkPipelineLayout m_graphicsPipelineLayout = VK_NULL_HANDLE;

    void CreateComputePipeline();
    void CreateGraphicsPipeline(VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout sharedSetLayout, VkSampleCountFlagBits msaaSamples);
};