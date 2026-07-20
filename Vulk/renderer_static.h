#pragma once

#include <vulkan/vulkan_core.h>
#include <vector>
#include <string>
#include <array>
#include <unordered_map>
#include <glm/glm.hpp>
#include <unordered_set>

#include "gpu_instances.h"   
#include "scene_types.h"
#include "mesh.h"            

class VulkanRenderer;
class Scene;

struct MeshBufferAllocation {
    uint32_t firstIndex;
    int32_t vertexOffset;
    std::vector<SubMesh> subMeshes;
    float maxBoundingRadius;
};

struct ChunkInstanceBucket {
    glm::vec3 chunkCenter;
    float chunkRadius;
    std::vector<glm::mat4> transforms;
};

struct InstancedGroup {
    // Trees are now grouped by their Chunk ID!
    std::unordered_map<int64_t, ChunkInstanceBucket> chunkBuckets;
};

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

private:
    static constexpr VkDeviceSize MAX_GLOBAL_VERTICES = 5'000'000;
    static constexpr VkDeviceSize MAX_GLOBAL_INDICES = 10'000'000;
    static constexpr uint32_t FRAMES_IN_FLIGHT_COUNT = 2;

    VkDevice m_device = VK_NULL_HANDLE;
    VulkanRenderer* m_renderer = nullptr;

    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;

    // Fast-mapping Instance Buffer
    VkBuffer m_instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_instanceMemory = VK_NULL_HANDLE;
    uint32_t m_maxInstances = 1000000;

    VkDeviceSize m_maxVertices = 0;
    VkDeviceSize m_maxIndices = 0;

    // Deduplication tracking
    std::unordered_map<std::string, MeshBufferAllocation> m_meshAllocations;
    std::unordered_map<std::string, InstancedGroup> m_instancedGroups;

    std::vector<SceneObject> m_sceneObjects;
    std::vector<SubMesh> m_subMeshes;
    std::vector<DrawEntry> m_staticDrawList;
    std::vector<DrawEntry> m_visibleStaticDrawList;

    std::array<VkQueryPool, FRAMES_IN_FLIGHT_COUNT> m_queryPools = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    std::array<std::vector<uint64_t>, FRAMES_IN_FLIGHT_COUNT> m_visibilityBuffers;
    std::array<bool, FRAMES_IN_FLIGHT_COUNT> m_hasIssuedQueries = { false, false };

    glm::vec3 m_lastCameraPos = glm::vec3(0.0f);
    glm::vec3 m_lastFrustumNormal = glm::vec3(0.0f);
    uint32_t m_sampleJitterCounter = 0;

    void UploadUniqueMeshes(const std::unordered_set<std::string>& uniqueMeshNames);
    void ResizeBuffers(uint32_t requiredVertices, uint32_t requiredIndices);
};