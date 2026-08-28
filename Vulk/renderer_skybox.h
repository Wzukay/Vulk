#pragma once

#include <vulkan/vulkan_core.h>
#include <string>

#include "asset_manager.h"   

class SkyboxRenderer {
public:
    // CHANGED: Replaced VkRenderPass with VkFormat colorFormat and depthFormat
    void Init(VkDevice device, VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout sharedSetLayout, VkSampleCountFlagBits msaaSamples);

    void LoadTexture(const std::string& folder = "assets/skybox/");
    void UpdateDescriptor(VkDevice device, VkDescriptorSet sharedDescriptorSet) const;
    void Draw(VkCommandBuffer commandBuffer, VkDescriptorSet sharedDescriptorSet) const;
    void Cleanup(VkDevice device);

    bool IsReady() const { return m_skyboxTexture.imageView != VK_NULL_HANDLE; }

private:
    VkPipeline skyboxPipeline = VK_NULL_HANDLE;
    VkPipelineLayout skyboxPipelineLayout = VK_NULL_HANDLE;
    VkShaderModule skyboxVertModule = VK_NULL_HANDLE;
    VkShaderModule skyboxFragModule = VK_NULL_HANDLE;

    Texture m_skyboxTexture;
};