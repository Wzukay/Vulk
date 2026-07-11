#pragma once

#include <vulkan/vulkan_core.h>
#include <vector>
#include <functional>
#include <mutex>
#include <deque>
#include <atomic>

class VulkanRenderer;

struct CopyRegion {
    const void* srcData;
    VkDeviceSize size;
    VkBuffer dstBuffer;
    VkDeviceSize dstOffset;
};

class RingBufferUploader {
public:
    ~RingBufferUploader() { Shutdown(); }

    void Init(VulkanRenderer* renderer, VkDeviceSize bufferSize = 32 * 1024 * 1024);
    void Shutdown();

    // Queue a single upload
    VkFence QueueUpload(const void* data, VkDeviceSize size,
        VkBuffer dstBuffer, VkDeviceSize dstOffset,
        std::function<void()> onComplete = nullptr);

    // Queue multiple uploads in one command buffer (more efficient)
    VkFence QueueBatchUpload(const std::vector<CopyRegion>& regions,
        std::function<void()> onComplete = nullptr);

    void Tick(uint64_t currentFrame);

    VkCommandPool GetCommandPool() const { return m_uploadCommandPool; }

private:
    VulkanRenderer* m_renderer = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;

    VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
    void* m_mappedData = nullptr;
    VkDeviceSize m_bufferSize = 0;
    VkDeviceSize m_writeOffset = 0;
    std::mutex m_mutex;

    VkCommandPool m_uploadCommandPool = VK_NULL_HANDLE;

    struct PendingUpload {
        VkFence fence = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        std::function<void()> onComplete;
        uint64_t submitFrame = 0;
    };
    std::deque<PendingUpload> m_pendingUploads;

    VkFence CreateFence();
    VkCommandBuffer BeginCommandBuffer();
    void SubmitCommandBuffer(VkCommandBuffer cmd, VkFence fence);
};