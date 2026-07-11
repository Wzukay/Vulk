#pragma once

#include <vulkan/vulkan_core.h>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <glm/glm.hpp>

#include "mesh.h"  
#include "gpu_async.h"
#include "ring_buffer_uploader.h"

class VulkanRenderer;

struct PushConstants {
    glm::mat4 modelMatrix;      // 64 bytes
    uint32_t textureId;         // 4 bytes
    uint32_t normalTextureId;   // 4 bytes
    uint32_t objectId;          // 4 bytes
    float lodBlend;
};

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

struct FreeSpan { uint32_t offset; uint32_t count; };
struct SpanReturn {
    FreeSpan vertexSpan;
    FreeSpan indexSpan;
};

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

    void Draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
        VkDescriptorSet descriptorSet, const glm::vec3& cameraPos,
        uint32_t& outDrawCalls, uint32_t& outCulledCount,
        uint32_t& outVertexCount, uint32_t& outIndexCount);

    float GetCachedHeight(float worldX, float worldZ);
    void ClearHeightCache();

    bool IsChunkOccluded(const glm::vec3& chunkCenter, float chunkRadius,
        const glm::vec3& cameraPos);

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VulkanRenderer* m_renderer = nullptr;
    RingBufferUploader* m_uploader = nullptr;

    // GPU buffers
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;

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

    // Deferred span returns (free list delayed by MAX_FRAMES_IN_FLIGHT)
    DeferredQueue<SpanReturn> m_pendingSpanReturns;

    float m_chunkSize = 512.0f;

    // Height cache for occlusion culling
    struct HeightCacheEntry { float height; bool valid; };
    std::unordered_map<int64_t, HeightCacheEntry> m_heightCache;
    std::mutex m_heightCacheMutex;

    // Occlusion culling state
    glm::vec3 m_lastOcclusionCameraPos = glm::vec3(0.0f);
    bool m_firstOcclusionUpdate = true;

    // Internal helpers
    void UploadTerrainChunkAsync(TerrainChunkGPU& chunk,
        const std::vector<ModelVertex>& verts,
        const std::vector<uint32_t>& indices);
    std::pair<uint32_t, uint32_t> AllocateSpace(uint32_t vertexCount, uint32_t indexCount);
    void DeferSpanReturn(const FreeSpan& vertexSpan, const FreeSpan& indexSpan, uint64_t safeFrame);
    bool IsChunkOccludedInternal(const glm::vec3& chunkCenter, float chunkRadius,
        const glm::vec3& cameraPos);
    float GetCachedHeightInternal(float worldX, float worldZ);
};