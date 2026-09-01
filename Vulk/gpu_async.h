#pragma once

#include <vector>
#include <functional>
#include <vulkan/vulkan_core.h>

// ---------------------------------------------------------------------
// 1) Deferred (safe-frame-gated) actions
// ---------------------------------------------------------------------

struct BufferDeletion {
    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceMemory> memories;
};

template <typename TPayload>
class DeferredQueue {
public:
    void Push(TPayload payload, uint64_t safeFrame) {
        m_items.push_back({ std::move(payload), safeFrame });
    }

    template <typename F>
    void Flush(uint64_t currentFrame, F&& onReady) {
        for (auto it = m_items.begin(); it != m_items.end(); ) {
            if (currentFrame >= it->safeFrame) {
                onReady(it->payload);
                it = m_items.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    bool Empty() const { return m_items.empty(); }
    size_t Size() const { return m_items.size(); }

private:
    struct Item {
        TPayload payload;
        uint64_t safeFrame;
    };
    std::vector<Item> m_items;
};

// ---------------------------------------------------------------------
// 2) In-flight fenced uploads
// ---------------------------------------------------------------------

struct StagingAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

template <typename TPayload>
struct PendingGPUUpload {
    VkFence fence = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    StagingAllocation staging[2];

    int64_t chunkKey = 0;
    TPayload payload;

    bool cancelled = false;
};