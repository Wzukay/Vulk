#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

#include "gpu_async.h"
#include "gpu_instances.h"
#include "mesh.h"
#include "ring_buffer_uploader.h"

class VulkanRenderer;

class WaterRenderer {
public:
    void Init(VkDevice device, VulkanRenderer* renderer, RingBufferUploader* uploader, VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout globalLayout, VkSampleCountFlagBits msaaSamples);
    void Cleanup();
    void Tick(uint64_t currentFrame);

    void AddWaterChunk(int64_t key, const std::vector<ModelVertex>& vertices, const std::vector<uint32_t>& indices);
    void RemoveWaterChunk(int64_t key);

    void SetSceneDepth(VkImageView depthView, VkSampler depthSampler);

    void Cull(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos, const glm::mat4& viewProj, uint32_t frameIndex);
    void Draw(VkCommandBuffer commandBuffer, VkDescriptorSet globalSet, const glm::vec3& cameraPos, float time, uint32_t frameIndex);

private:
    static constexpr uint32_t MAX_WATER_CHUNKS = 1024;
    static constexpr uint32_t MAX_WATER_VERTICES = 1'000'000;
    static constexpr uint32_t MAX_WATER_INDICES = 6'000'000;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

    VkDevice m_device = VK_NULL_HANDLE;
    VulkanRenderer* m_renderer = nullptr;
    RingBufferUploader* m_uploader = nullptr;
    uint32_t m_framesInFlight = 3;

    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;

    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    VkPipeline m_cullPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_cullPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_cullDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_cullDescriptorPool = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_depthDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_depthDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_depthDescriptorSet = VK_NULL_HANDLE;

    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_cullDescriptorSets{};
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_chunkDataBuffers{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> m_chunkDataMemories{};
    std::array<WaterChunkGPUData*, MAX_FRAMES_IN_FLIGHT> m_chunkDataMappedPtrs{};
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_indirectBuffers{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> m_indirectMemories{};
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_drawCountBuffers{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> m_drawCountMemories{};

    uint32_t m_submittedChunkCount = 0;

    std::unordered_map<int64_t, WaterChunk> m_chunks;
    std::vector<FreeSpan> m_freeVertexSpans;
    std::vector<FreeSpan> m_freeIndexSpans;
    std::atomic<uint32_t> m_nextVertexOffset{ 0 };
    std::atomic<uint32_t> m_nextIndexOffset{ 0 };
    DeferredQueue<SpanReturn> m_pendingSpanReturns;

    void CreatePipeline(VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout globalLayout, VkSampleCountFlagBits msaaSamples);
    void CreateCullPipeline();
    void CreateCullDescriptors();
    void UpdateCullDescriptors();

    std::pair<uint32_t, uint32_t> AllocateSpace(uint32_t vertexCount, uint32_t indexCount);
    void DeferSpanReturn(const WaterChunk& chunk);
};