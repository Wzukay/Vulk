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

struct PendingUpload {
    VkFence fence = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    std::function<void()> onComplete;
    uint64_t submitFrame = 0;

    VkDeviceSize stagingOffset = 0;
    VkDeviceSize stagingSize = 0;
};

class RingBufferUploader {
public:
    ~RingBufferUploader() { Shutdown(); }

    void Init(VulkanRenderer* renderer, VkDeviceSize bufferSize = 32 * 1024 * 1024);
    void Shutdown();

    VkFence QueueUpload(const void* data, VkDeviceSize size,
        VkBuffer dstBuffer, VkDeviceSize dstOffset,
        std::function<void()> onComplete = nullptr);

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
    VkDeviceSize m_readOffset = 0;
    VkDeviceSize m_stagingUsed = 0;
    std::mutex m_mutex;

    VkCommandPool m_uploadCommandPool = VK_NULL_HANDLE;

    std::deque<PendingUpload> m_pendingUploads;

    VkFence CreateFence();
    VkCommandBuffer BeginCommandBuffer();
    void SubmitCommandBuffer(VkCommandBuffer cmd, VkFence fence);
    bool TryReserveStagingSpace(VkDeviceSize size, VkDeviceSize& outOffset);
    void RetireCompletedUploads();
    void CompleteFrontUpload();
};