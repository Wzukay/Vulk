#pragma once

#include <vulkan/vulkan_core.h>
#include <string>

#include "assetManager.h"   // for Texture

// Owns everything needed to draw the skybox: its pipeline/shader modules
// and cubemap texture.
//
// The skybox intentionally does NOT own a private descriptor set - it draws
// through the renderer's shared global descriptor set (same UBO binding 0
// and cubemap binding 4 that the main pass uses), since that's what the
// skybox shaders are written against. UpdateDescriptor()/Draw() both take
// that descriptor set as a parameter rather than storing a pointer to it,
// so this class has no hidden dependency on VulkanRenderer's internals.
class SkyboxRenderer {
public:
    // Creates the pipeline + shader modules. Call once, after the render
    // pass and the shared descriptor set layout exist.
    void Init(VkDevice device, VkRenderPass renderPass, VkDescriptorSetLayout sharedSetLayout, VkSampleCountFlagBits msaaSamples);

    // Loads the cubemap faces from disk. Safe to call even if it fails -
    // Draw() just no-ops until a valid texture is loaded.
    void LoadTexture(const std::string& folder = "assets/skybox/");

    // Writes the cubemap into binding 4 of the given (shared) descriptor
    // set. Falls back to the asset manager's default texture if the
    // cubemap failed to load, so binding 4 is never left unwritten.
    void UpdateDescriptor(VkDevice device, VkDescriptorSet sharedDescriptorSet) const;

    // Binds the skybox pipeline and draws a single fullscreen triangle.
    // No-op if the cubemap never loaded.
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