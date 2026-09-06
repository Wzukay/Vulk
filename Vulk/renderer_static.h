#pragma once

#include <vulkan/vulkan_core.h>
#include <vector>
#include <string>
#include <array>
#include <unordered_map>
#include <glm/glm.hpp>
#include <unordered_set>

#include "gpu_instances.h"   
#include "mesh_types.h"  

class VulkanRenderer;
class Scene;

class StaticMeshRenderer {
public:
    void Init(VkDevice device, VulkanRenderer* renderer);
    void Cleanup();

    void UpdateScene(const Scene& scene);

    void Draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
        VkDescriptorSet descriptorSet, const glm::vec3& cameraPos,
        const std::array<FrustumPlane, 6>& frustumPlanes,
        uint32_t currentFrameIndex,
        VkPipeline staticPipeline, VkPipeline instancedPipeline, // Pipelines passed securely
        uint32_t& outDrawCalls, uint32_t& outCulledCount,
        uint32_t& outVertexCount, uint32_t& outIndexCount);

    void Cull(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos, const glm::mat4& viewProj, const glm::vec2& hzbSize,
        uint32_t currentFrameIndex, uint32_t& outCulledCount, uint32_t& outVertexCount, uint32_t& outIndexCount);
    void UpdateHZBDescriptor(VkImageView hzbView, VkSampler hzbSampler);

    void SetChunkSize(float chunkSize) {
        m_chunkSize = chunkSize;
    }

private:
    static constexpr VkDeviceSize MAX_GLOBAL_VERTICES = 5'000'000;
    static constexpr VkDeviceSize MAX_GLOBAL_INDICES = 10'000'000;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT_COUNT = 3;
    static constexpr uint32_t MAX_INDIRECT_BATCHES = 2'048;
    uint32_t m_framesInFlight = 3;

    float m_chunkSize = 512.0f;

    VkDevice m_device = VK_NULL_HANDLE;
    VulkanRenderer* m_renderer = nullptr;

    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;

    // Immutable-for-a-frame cull input and device-local visible output.
    VkBuffer m_instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_instanceMemory = VK_NULL_HANDLE;
    VkBuffer m_cullInputBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_cullInputMemory = VK_NULL_HANDLE;
    StaticInstanceCullData* m_mappedCullInput = nullptr;
    VkDeviceSize m_cullInputStride = 0;

    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT_COUNT> m_visibleInstanceBuffers = { VK_NULL_HANDLE };
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT_COUNT> m_visibleInstanceMemories = { VK_NULL_HANDLE };
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT_COUNT> m_indirectCommandBuffers = { VK_NULL_HANDLE };
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT_COUNT> m_indirectCommandMemories = { VK_NULL_HANDLE };

    // GPU to CPU Readback Buffers
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT_COUNT> m_indirectReadbackBuffers = { VK_NULL_HANDLE };
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT_COUNT> m_indirectReadbackMemories = { VK_NULL_HANDLE };
    std::array<void*, MAX_FRAMES_IN_FLIGHT_COUNT> m_mappedIndirectReadback = { nullptr };

    // Tracking historical data for the 2-frame UI delay
    std::array<uint32_t, MAX_FRAMES_IN_FLIGHT_COUNT> m_lastSubmittedInstances = { 0 };
    std::array<uint32_t, MAX_FRAMES_IN_FLIGHT_COUNT> m_lastBatchCount = { 0 };

    VkPipeline m_cullPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_cullPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_cullDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_cullDescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT_COUNT> m_cullDescriptorSets = { VK_NULL_HANDLE };
    uint32_t m_maxInstances = 1000000;

    VkDeviceSize m_maxVertices = 0;
    VkDeviceSize m_maxIndices = 0;

    // Deduplication tracking
    std::unordered_map<std::string, MeshBufferAllocation> m_meshAllocations;
    std::vector<StaticInstanceCullData> m_cullInstances;
    std::vector<StaticIndirectBatch> m_indirectBatches;

    std::vector<SceneObject> m_sceneObjects;
    std::vector<SubMesh> m_subMeshes;
    std::vector<DrawEntry> m_staticDrawList;
    std::vector<DrawEntry> m_visibleStaticDrawList;

    std::array<VkQueryPool, MAX_FRAMES_IN_FLIGHT_COUNT> m_queryPools = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    std::array<std::vector<uint64_t>, MAX_FRAMES_IN_FLIGHT_COUNT> m_visibilityBuffers;
    std::array<bool, MAX_FRAMES_IN_FLIGHT_COUNT> m_hasIssuedQueries = { false, false };

    glm::vec3 m_lastCameraPos = glm::vec3(0.0f);
    glm::vec3 m_lastFrustumNormal = glm::vec3(0.0f);
    uint32_t m_sampleJitterCounter = 0;

    void UploadUniqueMeshes(const std::unordered_set<std::string>& uniqueMeshNames);
    void ResizeBuffers(uint32_t requiredVertices, uint32_t requiredIndices);
    void CreateCullPipeline();
    void UpdateCullDescriptors();
};
