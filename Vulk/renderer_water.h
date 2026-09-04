#pragma once
#include <vulkan/vulkan_core.h>
#include <vector>
#include <glm/glm.hpp>

#include "ring_buffer_uploader.h"
#include "mesh.h"

class VulkanRenderer;

struct WaterPushConstants {
    glm::vec3 cameraPos;
    float time;
};

struct WaterChunk {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
};

class WaterRenderer {
public:
    void Init(VkDevice device, VulkanRenderer* renderer, RingBufferUploader* uploader, VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout globalLayout, VkSampleCountFlagBits msaaSamples);
    void Cleanup();
    void Draw(VkCommandBuffer cmd, VkDescriptorSet globalSet, const glm::vec3& camPos, float time);

    void AddWaterChunk(int64_t key, const std::vector<ModelVertex>& vertices, const std::vector<uint32_t>& indices);
    void RemoveWaterChunk(int64_t key);

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VulkanRenderer* m_renderer = nullptr;
    RingBufferUploader* m_uploader = nullptr;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    std::unordered_map<int64_t, WaterChunk> m_chunks;

    void CreatePipeline(VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout globalLayout, VkSampleCountFlagBits msaaSamples);
};