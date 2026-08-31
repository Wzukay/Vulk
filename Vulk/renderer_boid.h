#pragma once
#include <vulkan/vulkan_core.h>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <glm/glm.hpp>

class VulkanRenderer;

struct BoidInstance {
    alignas(16) glm::vec4 position; // xyz: position, w: scale
    alignas(16) glm::vec4 velocity; // xyz: velocity, w: animation time
};

struct BoidComputeParams {
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
    float pad[3];
};

struct BoidSwarmGPU {
    uint32_t boidOffset = 0;
    uint32_t boidCount = 0;
    uint32_t textureId = 0;
    int pingPongIndex = 0;

    VkDescriptorSet computeSets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

    glm::vec3 baseCenter = glm::vec3(0.0f);
    glm::vec3 currentCenter = glm::vec3(0.0f);
    float lifeTime = 0.0f;
};

class BoidRenderer {
public:
    void Init(VkDevice device, VulkanRenderer* renderer, VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout sharedSetLayout, VkSampleCountFlagBits msaaSamples);
    void Cleanup();

    void AddSwarm(int64_t chunkKey, const std::vector<BoidInstance>& initialBoids, uint32_t textureId);
    void RemoveSwarm(int64_t chunkKey);
    void TickCompute(VkCommandBuffer computeCmd, float deltaTime);
    void Draw(VkCommandBuffer drawCmd, VkDescriptorSet sharedDescriptorSet, uint32_t& outDrawCalls);

    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

private:
    struct BoidGarbage {
        VkDescriptorSet sets[2];
        uint64_t safeFrame;
    };
    std::vector<BoidGarbage> m_garbageSets;

    struct FreeSpan { uint32_t offset; uint32_t count; };
    std::vector<FreeSpan> m_freeBoidSpans;
    std::atomic<uint32_t> m_nextBoidOffset{ 0 };
    std::mutex m_allocMutex;

    uint32_t AllocateSpace(uint32_t boidCount);

    uint32_t m_maxBoids = 100000;
    VkBuffer m_boidBuffers[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory m_boidMemories[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

    VkDevice m_device = VK_NULL_HANDLE;
    VulkanRenderer* m_renderer = nullptr;

    std::unordered_map<int64_t, BoidSwarmGPU> m_swarms;

    VkPipeline m_computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_computeSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    VkPipelineLayout m_graphicsPipelineLayout = VK_NULL_HANDLE;

    void CreateComputePipeline();
    void CreateGraphicsPipeline(VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout sharedSetLayout, VkSampleCountFlagBits msaaSamples);
};