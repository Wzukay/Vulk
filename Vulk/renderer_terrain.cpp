#include "renderer_terrain.h"
#include "renderer.h"
#include "chunk.h"
#include <algorithm>
#include <iostream>
#include <glm/gtx/norm.hpp>

void TerrainRenderer::Init(VkDevice device, VulkanRenderer* renderer,
    RingBufferUploader* uploader,
    uint32_t maxVertices, uint32_t maxIndices) {
    m_device = device;
    m_renderer = renderer;
    m_uploader = uploader;
    m_maxVertices = maxVertices;
    m_maxIndices = maxIndices;

    VkDeviceSize vertexSize = sizeof(ModelVertex) * maxVertices;
    VkDeviceSize indexSize = sizeof(uint32_t) * maxIndices;

    m_renderer->CreateBuffer(vertexSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_vertexBuffer, m_vertexMemory);

    m_renderer->CreateBuffer(indexSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_indexBuffer, m_indexMemory);

    std::cout << "[TerrainRenderer] Initialized with " << maxVertices << " vertices, "
        << maxIndices << " indices.\n";
}

void TerrainRenderer::Cleanup() {
    if (m_device == VK_NULL_HANDLE) return;

    // Wait for all pending uploads (the uploader is shared, so we just wait for its fences)
    // The uploader's Tick will have cleaned up completed ones, but we need to make sure.
    // We'll let the uploader Shutdown handle it; we don't own the uploader.
    // So we just destroy our own buffers.

    m_renderer->DestroyBuffer(m_vertexBuffer, m_vertexMemory);
    m_renderer->DestroyBuffer(m_indexBuffer, m_indexMemory);

    m_terrainChunks.clear();
    m_freeVertexSpans.clear();
    m_freeIndexSpans.clear();
    m_heightCache.clear();
}

void TerrainRenderer::Tick(uint64_t currentFrame) {
    // Flush deferred span returns
    m_pendingSpanReturns.Flush(currentFrame, [&](SpanReturn& ret) {
        std::lock_guard<std::mutex> lock(m_terrainAllocMutex);
        m_freeVertexSpans.push_back(ret.vertexSpan);
        m_freeIndexSpans.push_back(ret.indexSpan);
        });
}

void TerrainRenderer::AddTerrainChunk(int64_t key, int cx, int cz, int lod,
    const std::vector<ModelVertex>& vertices,
    const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) return;

    auto it = m_pendingFlags.find(key);
    if (it != m_pendingFlags.end()) {
        *it->second = true; // mark cancelled
        m_pendingFlags.erase(it);
    }

    TerrainChunkGPU chunk{};
    chunk.key = key;
    chunk.lod = lod;

    glm::vec3 minBound(FLT_MAX), maxBound(-FLT_MAX);
    for (const auto& v : vertices) {
        minBound = glm::min(minBound, v.pos);
        maxBound = glm::max(maxBound, v.pos);
    }
    chunk.center = (minBound + maxBound) * 0.5f;
    float radiusSq = 0.0f;
    for (const auto& v : vertices) {
        float d2 = glm::length2(v.pos - chunk.center);
        if (d2 > radiusSq) radiusSq = d2;
    }
    chunk.radius = sqrt(radiusSq) + 10.0f;

    UploadTerrainChunkAsync(chunk, vertices, indices);
}

void TerrainRenderer::RemoveTerrainChunk(int64_t key) {
    auto it = m_pendingFlags.find(key);
    if (it != m_pendingFlags.end()) {
        *it->second = true;
        m_pendingFlags.erase(it);
    }

    auto it2 = m_terrainChunks.find(key);
    if (it2 == m_terrainChunks.end()) return;

    DeferSpanReturn({ it2->second.vertexOffset, it2->second.vertexCount },
        { it2->second.indexOffset, it2->second.indexCount },
        m_renderer->GetFrameCounter() + 3);
    m_terrainChunks.erase(it2);
}

void TerrainRenderer::Draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
    VkDescriptorSet descriptorSet, const glm::vec3& cameraPos,
    uint32_t& outDrawCalls, uint32_t& outCulledCount,
    uint32_t& outVertexCount, uint32_t& outIndexCount) {
    if (m_terrainChunks.empty()) return;

    VkBuffer vertexBuffers[] = { m_vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    const float occlusionRefreshDist = m_chunkSize * 0.25f;
    bool refreshOcclusion = m_firstOcclusionUpdate ||
        glm::length2(cameraPos - m_lastOcclusionCameraPos) > (occlusionRefreshDist * occlusionRefreshDist);

    if (refreshOcclusion) {
        m_lastOcclusionCameraPos = cameraPos;
        m_firstOcclusionUpdate = false;
    }

    for (auto& [key, chunk] : m_terrainChunks) {
        if (!chunk.ready) continue;

        if (!m_renderer->IsSphereInFrustum(chunk.center, chunk.radius)) {
            outCulledCount++;
            continue;
        }

        if (refreshOcclusion) {
            chunk.cachedOccluded = IsChunkOccludedInternal(chunk.center, chunk.radius, cameraPos);
        }
        if (chunk.cachedOccluded) {
            outCulledCount++;
            continue;
        }

        outVertexCount += chunk.vertexCount;
        outIndexCount += chunk.indexCount;

        float dist = glm::length(chunk.center - cameraPos);
        float blend = 0.0f;
        if (chunk.lod == 0) {
            float morphEnd = m_chunkSize * 2.0f;
            float t = glm::clamp(dist / morphEnd, 0.0f, 1.0f);
            blend = t * t * (3.0f - 2.0f * t);
        }

        PushConstants constants{};
        constants.modelMatrix = glm::mat4(1.0f);
        constants.textureId = 0;
        constants.normalTextureId = 0;
        constants.objectId = 0;
        constants.lodBlend = blend;

        vkCmdPushConstants(commandBuffer, pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(PushConstants), &constants);

        vkCmdDrawIndexed(commandBuffer,
            chunk.indexCount,
            1,
            chunk.indexOffset,
            chunk.vertexOffset,
            0);

        outDrawCalls++;
        outVertexCount += chunk.vertexCount;
        outIndexCount += chunk.indexCount;
    }
}

// ---- private methods ----

std::pair<uint32_t, uint32_t> TerrainRenderer::AllocateSpace(uint32_t vertexCount, uint32_t indexCount) {
    std::lock_guard<std::mutex> lock(m_terrainAllocMutex);

    auto vIt = std::find_if(m_freeVertexSpans.begin(), m_freeVertexSpans.end(),
        [&](const FreeSpan& s) { return s.count >= vertexCount; });
    auto iIt = std::find_if(m_freeIndexSpans.begin(), m_freeIndexSpans.end(),
        [&](const FreeSpan& s) { return s.count >= indexCount; });

    uint32_t vertexOffset, indexOffset;

    if (vIt != m_freeVertexSpans.end()) {
        vertexOffset = vIt->offset;
        m_freeVertexSpans.erase(vIt);
    }
    else {
        uint32_t current = m_nextVertexOffset.fetch_add(vertexCount);
        if (current + vertexCount > m_maxVertices) {
            throw std::runtime_error("Terrain vertex buffer overflow");
        }
        vertexOffset = current;
    }

    if (iIt != m_freeIndexSpans.end()) {
        indexOffset = iIt->offset;
        m_freeIndexSpans.erase(iIt);
    }
    else {
        uint32_t current = m_nextIndexOffset.fetch_add(indexCount);
        if (current + indexCount > m_maxIndices) {
            throw std::runtime_error("Terrain index buffer overflow");
        }
        indexOffset = current;
    }

    return { vertexOffset, indexOffset };
}

void TerrainRenderer::DeferSpanReturn(const FreeSpan& vertexSpan, const FreeSpan& indexSpan, uint64_t safeFrame) {
    m_pendingSpanReturns.Push({ vertexSpan, indexSpan }, safeFrame);
}

void TerrainRenderer::UploadTerrainChunkAsync(TerrainChunkGPU& chunk,
    const std::vector<ModelVertex>& verts,
    const std::vector<uint32_t>& indices) {
    VkDeviceSize vertexSize = sizeof(ModelVertex) * verts.size();
    VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();

    if (vertexSize == 0 || indexSize == 0) return;

    auto [vOffset, iOffset] = AllocateSpace(static_cast<uint32_t>(verts.size()),
        static_cast<uint32_t>(indices.size()));

    chunk.vertexOffset = vOffset;
    chunk.indexOffset = iOffset;
    chunk.vertexCount = static_cast<uint32_t>(verts.size());
    chunk.indexCount = static_cast<uint32_t>(indices.size());

    // Prepare batch regions
    std::vector<CopyRegion> regions;
    regions.push_back({ verts.data(), vertexSize, m_vertexBuffer, vOffset * sizeof(ModelVertex) });
    regions.push_back({ indices.data(), indexSize, m_indexBuffer, iOffset * sizeof(uint32_t) });

    auto chunkPtr = std::make_shared<TerrainChunkGPU>(chunk);
    auto keyPtr = std::make_shared<int64_t>(chunk.key);
    auto cancelledFlag = std::make_shared<bool>(false);

    // Store the flag in the map for cancellation
    m_pendingFlags[chunk.key] = cancelledFlag;

    m_uploader->QueueBatchUpload(regions, [this, chunkPtr, keyPtr, cancelledFlag]() {
        // Remove from map (the upload is done; no need to cancel anymore)
        m_pendingFlags.erase(*keyPtr);

        if (*cancelledFlag) {
            // Upload was cancelled; free the allocated spans
            std::lock_guard<std::mutex> lock(m_terrainAllocMutex);
            m_freeVertexSpans.push_back({ chunkPtr->vertexOffset, chunkPtr->vertexCount });
            m_freeIndexSpans.push_back({ chunkPtr->indexOffset, chunkPtr->indexCount });
            return;
        }

        // Replace or insert
        auto oldIt = m_terrainChunks.find(*keyPtr);
        if (oldIt != m_terrainChunks.end()) {
            DeferSpanReturn({ oldIt->second.vertexOffset, oldIt->second.vertexCount },
                { oldIt->second.indexOffset, oldIt->second.indexCount },
                m_renderer->GetFrameCounter() + 3);
        }
        chunkPtr->ready = true;
        m_terrainChunks[*keyPtr] = *chunkPtr;
        });
}

float TerrainRenderer::GetCachedHeight(float worldX, float worldZ) {
    std::lock_guard<std::mutex> lock(m_heightCacheMutex);
    return GetCachedHeightInternal(worldX, worldZ);
}

void TerrainRenderer::ClearHeightCache() {
    std::lock_guard<std::mutex> lock(m_heightCacheMutex);
    m_heightCache.clear();
}

bool TerrainRenderer::IsChunkOccluded(const glm::vec3& chunkCenter, float chunkRadius,
    const glm::vec3& cameraPos) {
    return IsChunkOccludedInternal(chunkCenter, chunkRadius, cameraPos);
}

// ---- Internal height/occlusion helpers ----

float TerrainRenderer::GetCachedHeightInternal(float worldX, float worldZ) {
    const float GRID = 10.0f;
    int gx = (int)std::floor(worldX / GRID + 0.5f);
    int gz = (int)std::floor(worldZ / GRID + 0.5f);
    int64_t key = (static_cast<int64_t>(gx) << 32) | (static_cast<uint32_t>(gz));

    auto it = m_heightCache.find(key);
    if (it != m_heightCache.end() && it->second.valid) {
        return it->second.height;
    }

    float h = Chunk::GetHeight(worldX, worldZ);
    m_heightCache[key] = { h, true };
    return h;
}

bool TerrainRenderer::IsChunkOccludedInternal(const glm::vec3& chunkCenter, float chunkRadius,
    const glm::vec3& cameraPos) {
    float distToCenter = glm::length(chunkCenter - cameraPos);
    if (distToCenter < chunkRadius * 2.0f) return false;

    // Corner check
    const float CORNER_MARGIN = 15.0f;
    glm::vec3 offsets[8] = {
        glm::vec3(1,  1,  1), glm::vec3(1,  1, -1),
        glm::vec3(1, -1,  1), glm::vec3(1, -1, -1),
        glm::vec3(-1,  1,  1), glm::vec3(-1,  1, -1),
        glm::vec3(-1, -1,  1), glm::vec3(-1, -1, -1)
    };
    for (const auto& off : offsets) {
        glm::vec3 corner = chunkCenter + off * (chunkRadius * 0.9f);
        float terrainH = GetCachedHeightInternal(corner.x, corner.z);
        if (corner.y > terrainH + 15.0f) {
            return false;
        }
    }

    // Raycast
    glm::vec3 dir = glm::normalize(chunkCenter - cameraPos);
    float stepSize = 25.0f;
    int numSamples = (int)(distToCenter / stepSize) + 1;
    numSamples = glm::clamp(numSamples, 8, 40);

    float margin = 15.0f + distToCenter * 0.02f;
    margin = glm::clamp(margin, 15.0f, 40.0f);

    glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
    glm::vec3 up = glm::cross(dir, right);
    float offsetRadius = chunkRadius * 0.6f;
    glm::vec3 rayOffsets[3] = {
        glm::vec3(0.0f),
        right * offsetRadius + up * offsetRadius * 0.5f,
        -right * offsetRadius - up * offsetRadius * 0.5f
    };

    for (int i = 0; i < numSamples; ++i) {
        float t = (float)i / (float)numSamples * distToCenter;
        for (int r = 0; r < 3; ++r) {
            glm::vec3 samplePos = cameraPos + dir * (t + 2.0f) + rayOffsets[r];
            float terrainH = GetCachedHeightInternal(samplePos.x, samplePos.z);
            if (samplePos.y > terrainH + margin) {
                return false;
            }
        }
    }
    return true;
}