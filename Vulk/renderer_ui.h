#pragma once

#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

class UIRenderer {
public:
    void Init(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily, VkQueue graphicsQueue, uint32_t imageCount, VkRenderPass renderPass);
    void Recreate(VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t queueFamily, VkQueue graphicsQueue, uint32_t imageCount, VkRenderPass renderPass);
    void Cleanup();

    void BeginFrame();

    // --- NEW: Split UI Functions ---
    void DrawDebugStats(uint32_t sceneTotalIndices, uint32_t sceneTotalVertices, uint32_t drawCallCount, uint32_t culledCount);
    void DrawPlayerHUD(float currentHealthPercentage);

    void EndFrame();
    void RecordCommands(VkCommandBuffer commandBuffer);

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;

    void CreateDescriptorPool();
};