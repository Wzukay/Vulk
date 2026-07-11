#pragma once

#include <vulkan/vulkan_core.h>
#include <vector>
#include <string>
#include <array>
#include <glm/glm.hpp>

#include "gpu_instances.h"   // for FrustumPlane, DrawEntry, SceneObject, SubMesh
#include "scene_types.h"
#include "mesh.h"            // for ModelVertex

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
        uint32_t& outDrawCalls, uint32_t& outCulledCount,
        uint32_t& outVertexCount, uint32_t& outIndexCount);

private:
    static constexpr VkDeviceSize MAX_GLOBAL_VERTICES = 5'000'000;
    static constexpr VkDeviceSize MAX_GLOBAL_INDICES = 10'000'000;

    VkDevice m_device = VK_NULL_HANDLE;
    VulkanRenderer* m_renderer = nullptr;

    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;

    VkDeviceSize m_maxVertices = 0;
    VkDeviceSize m_maxIndices = 0;

    std::vector<SceneObject> m_sceneObjects;
    std::vector<SubMesh> m_subMeshes;
    std::vector<DrawEntry> m_staticDrawList;
    std::vector<DrawEntry> m_visibleStaticDrawList;
    std::vector<std::string> m_lastInstanceMeshNames;
    std::vector<std::vector<uint32_t>> m_objectDrawEntryIndices;

    void UploadStaticSceneData(const std::vector<ModelVertex>& verts,
        const std::vector<uint32_t>& idxs);
};