#pragma once
// Generic helpers for the two recurring GPU-async patterns in the renderer:
//
//  1) "Destroy/return this resource once it's safe
//
//  2) This upload is in flight behind a fence, do something with the
//     payload once it signals

#include <vector>
#include <functional>
#include <vulkan/vulkan_core.h>

// ---------------------------------------------------------------------
// 1) Deferred (safe-frame-gated) actions
// ---------------------------------------------------------------------

// A pair of GPU allocations to free together. Covers the "destroy a
// buffer+memory pair" case (grass instance buffer, water vertex/index
// buffers, terrain LOD replace, etc). Use an empty vector for whichever
// side isn't needed.
struct BufferDeletion {
    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceMemory> memories;
};

// Generic "do this once currentFrame >= safeFrame" queue. Works for any
// payload type — buffer deletions, span returns to a free-list, or
// anything else that needs to wait out MAX_FRAMES_IN_FLIGHT before it's
// safe (i.e. no in-flight command buffer can still be reading it).
template <typename TPayload>
class DeferredQueue {
public:
    void Push(TPayload payload, uint64_t safeFrame) {
        m_items.push_back({ std::move(payload), safeFrame });
    }

    // Calls onReady(payload) for every item whose safeFrame has passed,
    // then removes it from the queue.
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

// A staging buffer/memory pair. Terrain uses two of these (vertex+index);
// grass uses one. Leave a pair's fields VK_NULL_HANDLE when not used (e.g.
// terrain's ring-buffer staging path owns no per-upload staging memory).
struct StagingAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

// One in-flight async upload, tracked by a fence. TPayload is whatever the
// upload eventually produces (a TerrainChunkGPU, a GrassChunkGPU, ...).
template <typename TPayload>
struct PendingGPUUpload {
    VkFence fence = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    // Most uploads need at most two staging allocations (vertex + index);
    // grass only uses staging[0]. Ring-buffer-backed uploads (terrain)
    // leave both at VK_NULL_HANDLE since there's nothing per-upload to free.
    StagingAllocation staging[2];

    int64_t chunkKey = 0;
    TPayload payload;

    // Set true if the source chunk was removed/superseded while this
    // upload was still in flight. The completion handler should discard
    // the payload's device resources instead of publishing them.
    bool cancelled = false;
};