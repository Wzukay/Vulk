#pragma once

#include <vulkan/vulkan_core.h>

struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    void CleanUp(VkDevice device) {
        if (sampler != VK_NULL_HANDLE) vkDestroySampler(device, sampler, nullptr);
        if (imageView != VK_NULL_HANDLE) vkDestroyImageView(device, imageView, nullptr);
        if (image != VK_NULL_HANDLE) vkDestroyImage(device, image, nullptr);
        if (imageMemory != VK_NULL_HANDLE) vkFreeMemory(device, imageMemory, nullptr);
    }
};