#pragma once

#include <vulkan/vulkan_core.h>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <glm/glm.hpp>
#include <array>

#include "mesh.h"  
#include "gpu_async.h"
#include "ring_buffer_uploader.h"
#include "gpu_instances.h"

class VulkanRenderer;

class TerrainRenderer {
public:
    void Init(VkDevice device, VulkanRenderer* renderer,
        RingBufferUploader* uploader,
        uint32_t maxVertices, uint32_t maxIndices);
    void Cleanup();

    void Tick(uint64_t currentFrame);

    void AddTerrainChunk(int64_t key, int cx, int cz, int lod,
        const std::vector<ModelVertex>& vertices,
        const std::vector<uint32_t>& indices);
    void RemoveTerrainChunk(int64_t key);

    void SetChunkSize(float size) { m_chunkSize = size; }

    void Draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t currentFrameIndex, uint32_t& outDrawCalls);
    void Cull(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos, const glm::mat4& viewProj,
        glm::vec2 hzbSize, float maxMip, uint32_t currentFrameIndex,
        uint32_t& outCulledCount, uint32_t& outVertexCount, uint32_t& outIndexCount);

    float GetCachedHeight(float worldX, float worldZ);
    void ClearHeightCache();

    bool IsChunkOccluded(const glm::vec3& chunkCenter, float chunkRadius,
        const glm::vec3& cameraPos);

    void UpdateHZBDescriptor(VkImageView hzbView, VkSampler hzbSampler);

private:
    static constexpr uint32_t MAX_TERRAIN_CHUNKS = 1024;
    static constexpr uint32_t FRAMES_IN_FLIGHT = 3;

    float m_chunkSize = 512.0f;

    VkDevice m_device = VK_NULL_HANDLE;
    VulkanRenderer* m_renderer = nullptr;
    RingBufferUploader* m_uploader = nullptr;

    // GPU buffers
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;

    VkPipeline m_computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_computeDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_computeDescriptorPool = VK_NULL_HANDLE;

    // Per-frame compute buffers
    std::array<VkDescriptorSet, FRAMES_IN_FLIGHT> m_computeDescriptorSets = { VK_NULL_HANDLE };
    std::array<VkBuffer, FRAMES_IN_FLIGHT> m_chunkDataBuffers = { VK_NULL_HANDLE };
    std::array<VkDeviceMemory, FRAMES_IN_FLIGHT> m_chunkDataMemories = { VK_NULL_HANDLE };
    std::array<VkBuffer, FRAMES_IN_FLIGHT> m_indirectCommandBuffers = { VK_NULL_HANDLE };
    std::array<VkDeviceMemory, FRAMES_IN_FLIGHT> m_indirectCommandMemories = { VK_NULL_HANDLE };
    std::array<VkBuffer, FRAMES_IN_FLIGHT> m_drawCountBuffers = { VK_NULL_HANDLE };
    std::array<VkDeviceMemory, FRAMES_IN_FLIGHT> m_drawCountMemories = { VK_NULL_HANDLE };
    std::array<TerrainChunkGPUData*, FRAMES_IN_FLIGHT> m_chunkDataMappedPtrs = { nullptr };

    uint32_t m_cullChunkCount = 0;

    uint32_t m_maxVertices = 0;
    uint32_t m_maxIndices = 0;

    // Free‑list management
    std::vector<FreeSpan> m_freeVertexSpans;
    std::vector<FreeSpan> m_freeIndexSpans;
    std::atomic<uint32_t> m_nextVertexOffset{ 0 };
    std::atomic<uint32_t> m_nextIndexOffset{ 0 };
    std::mutex m_terrainAllocMutex;

    // Loaded terrain chunks
    std::unordered_map<int64_t, TerrainChunkGPU> m_terrainChunks;

    std::unordered_map<int64_t, std::shared_ptr<bool>> m_pendingFlags;
    DeferredQueue<SpanReturn> m_pendingSpanReturns;

    struct HeightCacheEntry { float height; bool valid; };
    std::unordered_map<int64_t, HeightCacheEntry> m_heightCache;
    std::mutex m_heightCacheMutex;

    glm::vec3 m_lastOcclusionCameraPos = glm::vec3(0.0f);
    bool m_firstOcclusionUpdate = true;

    void UploadTerrainChunkAsync(TerrainChunkGPU& chunk,
        const std::vector<ModelVertex>& verts,
        const std::vector<uint32_t>& indices);
    std::pair<uint32_t, uint32_t> AllocateSpace(uint32_t vertexCount, uint32_t indexCount);
    void DeferSpanReturn(const FreeSpan& vertexSpan, const FreeSpan& indexSpan, uint64_t safeFrame);
    bool IsChunkOccludedInternal(const glm::vec3& chunkCenter, float chunkRadius,
        const glm::vec3& cameraPos);
    float GetCachedHeightInternal(float worldX, float worldZ);

    void CreateComputePipeline();
    void UpdateComputeDescriptors();
};