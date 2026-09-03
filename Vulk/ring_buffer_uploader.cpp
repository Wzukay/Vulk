#include "ring_buffer_uploader.h"
#include "renderer.h"
#include <stdexcept>
#include <iostream>

void RingBufferUploader::Init(VulkanRenderer* renderer, VkDeviceSize bufferSize) {
    m_renderer = renderer;
    m_device = renderer->GetLogicalDevice();
    m_bufferSize = bufferSize;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_stagingBuffer) != VK_SUCCESS) {
        throw std::runtime_error("RingBufferUploader: failed to create staging buffer");
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(m_device, m_stagingBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = m_renderer->FindMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_stagingMemory) != VK_SUCCESS) {
        throw std::runtime_error("RingBufferUploader: failed to allocate staging memory");
    }

    vkBindBufferMemory(m_device, m_stagingBuffer, m_stagingMemory, 0);

    if (vkMapMemory(m_device, m_stagingMemory, 0, bufferSize, 0, &m_mappedData) != VK_SUCCESS) {
        throw std::runtime_error("RingBufferUploader: failed to map staging buffer");
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = m_renderer->FindQueueFamilies(m_renderer->GetPhysicalDevice()).graphicsFamily.value();

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_uploadCommandPool) != VK_SUCCESS) {
        throw std::runtime_error("RingBufferUploader: failed to create command pool");
    }

    std::cout << "[RingBufferUploader] Initialized with " << bufferSize / (1024 * 1024) << " MB staging buffer\n";
}

void RingBufferUploader::Shutdown() {
    if (m_device == VK_NULL_HANDLE) return;

    // Wait for all pending uploads
    for (auto& upload : m_pendingUploads) {
        if (upload.fence != VK_NULL_HANDLE) {
            vkWaitForFences(m_device, 1, &upload.fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(m_device, upload.fence, nullptr);
        }
        if (upload.commandBuffer != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(m_device, m_uploadCommandPool, 1, &upload.commandBuffer);
        }
    }

    m_pendingUploads.clear();

    m_writeOffset = 0;
    m_readOffset = 0;
    m_stagingUsed = 0;

    // Destroy command pool first (it owns command buffers)
    if (m_uploadCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_uploadCommandPool, nullptr);
        m_uploadCommandPool = VK_NULL_HANDLE;
    }

    // Unmap before freeing memory
    if (m_mappedData) {
        vkUnmapMemory(m_device, m_stagingMemory);
        m_mappedData = nullptr;
    }

    // Free memory and destroy buffer
    if (m_stagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_stagingMemory, nullptr);
        m_stagingMemory = VK_NULL_HANDLE;
    }
    if (m_stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_stagingBuffer, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
    }
}

VkFence RingBufferUploader::QueueUpload(const void* data, VkDeviceSize size,
    VkBuffer dstBuffer, VkDeviceSize dstOffset,
    std::function<void()> onComplete) {
    std::vector<CopyRegion> regions;
    regions.push_back({ data, size, dstBuffer, dstOffset });
    return QueueBatchUpload(regions, onComplete);
}

VkFence RingBufferUploader::QueueBatchUpload(
    const std::vector<CopyRegion>& regions,
    std::function<void()> onComplete)
{
    if (regions.empty()) {
        return VK_NULL_HANDLE;
    }

    VkDeviceSize totalSize = 0;
    for (const CopyRegion& region : regions) {
        if (region.srcData == nullptr || region.size == 0) {
            throw std::runtime_error(
                "RingBufferUploader: invalid copy region");
        }

        totalSize += region.size;
    }

    if (totalSize > m_bufferSize) {
        throw std::runtime_error(
            "RingBufferUploader: upload exceeds staging-buffer capacity");
    }

    // Reclaim already-completed uploads before considering a stall.
    RetireCompletedUploads();

    VkDeviceSize stagingOffset = 0;

    // If the ring is full, wait only for the oldest upload that owns the
    // required staging region. This preserves correctness without queue-idle.
    while (!TryReserveStagingSpace(totalSize, stagingOffset)) {
        if (m_pendingUploads.empty()) {
            throw std::runtime_error(
                "RingBufferUploader: unable to reserve staging space");
        }

        VkFence oldestFence = m_pendingUploads.front().fence;
        const VkResult waitResult = vkWaitForFences(
            m_device,
            1,
            &oldestFence,
            VK_TRUE,
            UINT64_MAX);

        if (waitResult != VK_SUCCESS) {
            throw std::runtime_error(
                "RingBufferUploader: failed waiting for staging space");
        }

        RetireCompletedUploads();
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        VkDeviceSize writeOffset = stagingOffset;
        for (const CopyRegion& region : regions) {
            std::memcpy(
                static_cast<char*>(m_mappedData) + writeOffset,
                region.srcData,
                static_cast<size_t>(region.size));

            writeOffset += region.size;
        }
    }

    VkCommandBuffer commandBuffer = BeginCommandBuffer();

    VkDeviceSize sourceOffset = stagingOffset;
    for (const CopyRegion& region : regions) {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = sourceOffset;
        copyRegion.dstOffset = region.dstOffset;
        copyRegion.size = region.size;

        vkCmdCopyBuffer(
            commandBuffer,
            m_stagingBuffer,
            region.dstBuffer,
            1,
            &copyRegion);

        sourceOffset += region.size;
    }

    VkFence fence = CreateFence();
    SubmitCommandBuffer(commandBuffer, fence);

    PendingUpload upload{};
    upload.fence = fence;
    upload.commandBuffer = commandBuffer;
    upload.onComplete = std::move(onComplete);
    upload.submitFrame = m_renderer->GetFrameCounter();
    upload.stagingOffset = stagingOffset;
    upload.stagingSize = totalSize;

    m_pendingUploads.push_back(std::move(upload));

    return fence;
}

void RingBufferUploader::Tick(uint64_t currentFrame) {
    (void)currentFrame;
    RetireCompletedUploads();
}

// ---- Private helpers ----

VkFence RingBufferUploader::CreateFence() {
    VkFenceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.flags = 0;
    VkFence fence;
    if (vkCreateFence(m_device, &info, nullptr, &fence) != VK_SUCCESS) {
        throw std::runtime_error("RingBufferUploader: failed to create fence");
    }
    return fence;
}

VkCommandBuffer RingBufferUploader::BeginCommandBuffer() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_uploadCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("RingBufferUploader: failed to allocate command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    return cmd;
}

void RingBufferUploader::SubmitCommandBuffer(VkCommandBuffer cmd, VkFence fence) {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkQueue queue = m_renderer->GetGraphicsQueue();
    if (vkQueueSubmit(queue, 1, &submitInfo, fence) != VK_SUCCESS) {
        throw std::runtime_error("RingBufferUploader: failed to submit command buffer");
    }
}
bool RingBufferUploader::TryReserveStagingSpace(VkDeviceSize size, VkDeviceSize& outOffset) {
    std::lock_guard<std::mutex> lock(m_mutex);

    static constexpr VkDeviceSize ALIGNMENT = 64;

    VkDeviceSize alignedSize = (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

    if (alignedSize > m_bufferSize || alignedSize > m_bufferSize - m_stagingUsed) {
        return false;
    }

    if (m_stagingUsed == 0) {
        m_readOffset = 0;
        m_writeOffset = 0;
        outOffset = 0;
        m_writeOffset = alignedSize % m_bufferSize;
        m_stagingUsed = alignedSize;
        return true;
    }

    if (m_writeOffset >= m_readOffset) {
        const VkDeviceSize tailSpace = m_bufferSize - m_writeOffset;

        if (alignedSize <= tailSpace) {
            outOffset = m_writeOffset;
            m_writeOffset = (m_writeOffset + alignedSize) % m_bufferSize;
            m_stagingUsed += alignedSize;
            return true;
        }

        if (alignedSize <= m_readOffset) {
            outOffset = 0;
            m_writeOffset = alignedSize;
            // Pad the lost tail space so tracking remains accurate
            m_stagingUsed += alignedSize + tailSpace;
            return true;
        }

        return false;
    }

    const VkDeviceSize gapSpace = m_readOffset - m_writeOffset;
    if (alignedSize <= gapSpace) {
        outOffset = m_writeOffset;
        m_writeOffset += alignedSize;
        m_stagingUsed += alignedSize;
        return true;
    }

    return false;
}

void RingBufferUploader::CompleteFrontUpload() {
    PendingUpload upload = std::move(m_pendingUploads.front());
    m_pendingUploads.pop_front();

    if (upload.onComplete) {
        upload.onComplete();
    }

    if (upload.fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_device, upload.fence, nullptr);
    }

    if (upload.commandBuffer != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(
            m_device,
            m_uploadCommandPool,
            1,
            &upload.commandBuffer);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_readOffset = (upload.stagingOffset + upload.stagingSize) % m_bufferSize;
        m_stagingUsed -= upload.stagingSize;

        if (m_stagingUsed == 0) {
            m_readOffset = 0;
            m_writeOffset = 0;
        }
    }
}

void RingBufferUploader::RetireCompletedUploads() {
    while (!m_pendingUploads.empty()) {
        const VkResult status = vkGetFenceStatus(
            m_device,
            m_pendingUploads.front().fence);

        if (status == VK_NOT_READY) {
            return;
        }

        if (status != VK_SUCCESS) {
            throw std::runtime_error(
                "RingBufferUploader: vkGetFenceStatus failed");
        }

        CompleteFrontUpload();
    }
}