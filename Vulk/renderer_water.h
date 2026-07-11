#pragma once

#include <vulkan/vulkan_core.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <chrono>
#include <glm/glm.hpp>

#include "mesh.h"          // WaterMesh, WaterVertex
#include "gpu_async.h"     // BufferDeletion

class VulkanRenderer;      // forward declaration

struct WaterBodyGPU {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
    uint32_t normalTextureId = 0;
    float tiling = 8.0f;
    float waveStrength = 0.15f;
};
struct WaterPushConstants {
    glm::mat4 modelMatrix;      // identity for now
    float time;
    uint32_t normalTextureId;
    float tiling;
    float waveStrength;
};

class WaterRenderer {
public:
    void Init(VkDevice device, VkRenderPass renderPass,
        VkDescriptorSetLayout sharedSetLayout,
        VkSampleCountFlagBits msaaSamples,
        VulkanRenderer* renderer);
    void Cleanup(VkDevice device);

    // Adds a water body associated with a chunk key (replaces if key exists)
    void AddWaterBodyForChunk(int64_t chunkKey, const WaterMesh& mesh,
        const std::string& normalMapPath,
        float tiling = 8.0f, float waveStrength = 0.15f);
    // Adds a water body without chunk association (simple vector append)
    void AddWaterBody(const WaterMesh& mesh, const std::string& normalMapPath,
        float tiling = 8.0f, float waveStrength = 0.15f);
    // Removes water body associated with chunk key
    void RemoveWaterBody(int64_t chunkKey);

    void Draw(VkCommandBuffer commandBuffer, VkDescriptorSet sharedDescriptorSet, uint32_t& outDrawCalls) const;

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VulkanRenderer* m_renderer = nullptr;

    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkShaderModule m_vertModule = VK_NULL_HANDLE;
    VkShaderModule m_fragModule = VK_NULL_HANDLE;

    std::vector<WaterBodyGPU> m_waterBodies;
    std::unordered_map<int64_t, size_t> m_waterBodyLookup;

    std::chrono::high_resolution_clock::time_point m_startTime =
        std::chrono::high_resolution_clock::now();

    // Internal helpers
    void CreatePipeline(VkRenderPass renderPass,
        VkDescriptorSetLayout sharedSetLayout,
        VkSampleCountFlagBits msaaSamples);
    size_t CreateWaterBodyGPU(const WaterMesh& mesh,
        const std::string& normalMapPath,
        float tiling, float waveStrength);
};