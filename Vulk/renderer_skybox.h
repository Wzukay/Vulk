#pragma once

#include <vulkan/vulkan_core.h>
#include <string>

#include "gpu_instances.h"
#include "asset_manager.h"   

class VulkanRenderer;

class SkyboxRenderer {
public:
    void Init(VkDevice device, VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout sharedSetLayout, VkSampleCountFlagBits msaaSamples);

    void UpdateDescriptor(VkDevice device, VkDescriptorSet sharedDescriptorSet) const;

    void Draw(VkCommandBuffer commandBuffer, VkDescriptorSet sharedDescriptorSet, const SkyboxPushConstants& pc) const;

    void Cleanup(VkDevice device);

    bool IsReady() const { return skyboxPipeline != VK_NULL_HANDLE; }

private:
    VkPipeline skyboxPipeline = VK_NULL_HANDLE;
    VkPipelineLayout skyboxPipelineLayout = VK_NULL_HANDLE;
    VkShaderModule skyboxVertModule = VK_NULL_HANDLE;
    VkShaderModule skyboxFragModule = VK_NULL_HANDLE;

    Texture m_skyboxTexture;
};