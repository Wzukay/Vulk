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
    m_framesInFlight = m_renderer->GetFramesInFlight();

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

    VkDeviceSize chunkDataSize = sizeof(TerrainChunkGPUData) * MAX_TERRAIN_CHUNKS;
    VkDeviceSize indirectSize = sizeof(VkDrawIndexedIndirectCommand) * MAX_TERRAIN_CHUNKS;

    // Allocate resources per frame-in-flight
    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        m_renderer->CreateBuffer(chunkDataSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_chunkDataBuffers[i], m_chunkDataMemories[i]);

        vkMapMemory(m_device, m_chunkDataMemories[i], 0, VK_WHOLE_SIZE, 0, (void**)&m_chunkDataMappedPtrs[i]);

        m_renderer->CreateBuffer(indirectSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_indirectCommandBuffers[i], m_indirectCommandMemories[i]);

        m_renderer->CreateBuffer(sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_drawCountBuffers[i], m_drawCountMemories[i]);
    }

    CreateComputePipeline();

    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * m_framesInFlight };
    poolSizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_framesInFlight };

    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, m_framesInFlight, 2, poolSizes };
    vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_computeDescriptorPool);

    std::vector<VkDescriptorSetLayout> layouts(m_framesInFlight, m_computeDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_computeDescriptorPool, m_framesInFlight, layouts.data() };
    vkAllocateDescriptorSets(m_device, &allocInfo, m_computeDescriptorSets.data());

    UpdateComputeDescriptors();

    std::cout << "[TerrainRenderer] Initialized with " << maxVertices << " vertices, "
        << maxIndices << " indices.\n";
}

void TerrainRenderer::Cleanup() {
    if (m_device == VK_NULL_HANDLE) return;

    if (m_computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_computePipeline, nullptr);
        m_computePipeline = VK_NULL_HANDLE;
    }
    if (m_computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_computePipelineLayout, nullptr);
        m_computePipelineLayout = VK_NULL_HANDLE;
    }
    if (m_computeDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_computeDescriptorSetLayout, nullptr);
        m_computeDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_computeDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_computeDescriptorPool, nullptr);
        m_computeDescriptorPool = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        if (m_chunkDataMappedPtrs[i] != nullptr) {
            vkUnmapMemory(m_device, m_chunkDataMemories[i]);
            m_chunkDataMappedPtrs[i] = nullptr;
        }

        m_renderer->DestroyBuffer(m_chunkDataBuffers[i], m_chunkDataMemories[i]);
        m_renderer->DestroyBuffer(m_indirectCommandBuffers[i], m_indirectCommandMemories[i]);
        m_renderer->DestroyBuffer(m_drawCountBuffers[i], m_drawCountMemories[i]);
    }

    m_renderer->DestroyBuffer(m_vertexBuffer, m_vertexMemory);
    m_renderer->DestroyBuffer(m_indexBuffer, m_indexMemory);

    m_terrainChunks.clear();
    m_freeVertexSpans.clear();
    m_freeIndexSpans.clear();
    m_heightCache.clear();
}

void TerrainRenderer::Tick(uint64_t currentFrame) {
    m_pendingSpanReturns.Flush(currentFrame, [&](SpanReturn& ret) {
        std::lock_guard<std::mutex> lock(m_terrainAllocMutex);
        m_freeVertexSpans.push_back(ret.vertexSpan);
        m_freeIndexSpans.push_back(ret.indexSpan);
        });

    // Fix 5: Prevent unbounded cache growth 
    std::lock_guard<std::mutex> lock(m_heightCacheMutex);
    if (m_heightCache.size() > 50000) {
        m_heightCache.clear();
    }
}

void TerrainRenderer::AddTerrainChunk(int64_t key, int cx, int cz, int lod,
    const std::vector<ModelVertex>& vertices,
    const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) return;

    auto it = m_pendingFlags.find(key);
    if (it != m_pendingFlags.end()) {
        *it->second = true;
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
        m_renderer->GetFrameCounter() + m_renderer->GetFramesInFlight());
    m_terrainChunks.erase(it2);
}

void TerrainRenderer::Cull(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos, const glm::mat4& viewProj,
    glm::vec2 hzbSize, float maxMip, uint32_t currentFrameIndex,
    uint32_t& outCulledCount, uint32_t& outVertexCount, uint32_t& outIndexCount) {

    if (m_terrainChunks.empty()) { m_cullChunkCount = 0; return; }

    TerrainChunkGPUData* chunkDataMapped = m_chunkDataMappedPtrs[currentFrameIndex];

    std::vector<std::pair<float, const TerrainChunkGPU*>> sortedChunks;
    sortedChunks.reserve(m_terrainChunks.size());

    uint32_t submittedVerts = 0;
    uint32_t submittedInds = 0;
    uint32_t chunkIndex = 0;

    for (const auto& [key, chunk] : m_terrainChunks) {
        if (!chunk.ready) continue;

        if (!m_renderer->IsWorldSphereInFrustum(chunk.center, chunk.radius)) {
            outCulledCount++;
            continue;
        }

        if (chunkIndex >= MAX_TERRAIN_CHUNKS) break;

        chunkDataMapped[chunkIndex] = {
            glm::vec4(chunk.center, chunk.radius),
            chunk.indexCount, chunk.indexOffset, chunk.vertexOffset, static_cast<uint32_t>(chunk.lod)
        };

        chunkIndex++;
        submittedVerts += chunk.vertexCount;
        submittedInds += chunk.indexCount;
    }

    // Push the active chunk geometry totals back to the main thread
    outVertexCount += submittedVerts;
    outIndexCount += submittedInds;
    m_cullChunkCount = chunkIndex;

    if (chunkIndex == 0) return;

    uint32_t zero = 0;
    vkCmdUpdateBuffer(commandBuffer, m_drawCountBuffers[currentFrameIndex], 0, sizeof(uint32_t), &zero);
    vkCmdFillBuffer(commandBuffer, m_indirectCommandBuffers[currentFrameIndex], 0, sizeof(VkDrawIndexedIndirectCommand) * chunkIndex, 0);

    VkMemoryBarrier transferBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &transferBarrier, 0, nullptr, 0, nullptr);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipelineLayout, 0, 1, &m_computeDescriptorSets[currentFrameIndex], 0, nullptr);

    ComputePush computePush{};
    computePush.viewProj = viewProj;
    computePush.cameraPos = cameraPos;
    computePush.totalChunks = chunkIndex;
    computePush.hzbSize = hzbSize;
    computePush.maxMip = maxMip;

    vkCmdPushConstants(commandBuffer, m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePush), &computePush);
    vkCmdDispatch(commandBuffer, (chunkIndex + 63) / 64, 1, 1);

    VkMemoryBarrier drawBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &drawBarrier, 0, nullptr, 0, nullptr);
}

void TerrainRenderer::Draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t currentFrameIndex, uint32_t& outDrawCalls) {
    if (m_cullChunkCount == 0) return;

    VkBuffer vertexBuffers[] = { m_vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    PushConstants defaultConstants{};
    defaultConstants.modelMatrix = glm::mat4(1.0f);
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &defaultConstants);

    vkCmdDrawIndexedIndirect(commandBuffer, m_indirectCommandBuffers[currentFrameIndex], 0, m_cullChunkCount, sizeof(VkDrawIndexedIndirectCommand));
    outDrawCalls++;
}

// ---- private methods ----

std::pair<uint32_t, uint32_t> TerrainRenderer::AllocateSpace(
    uint32_t vertexCount,
    uint32_t indexCount)
{
    std::lock_guard<std::mutex> lock(m_terrainAllocMutex);

    auto coalesceSpans = [](std::vector<FreeSpan>& spans) {
        if (spans.empty()) {
            return;
        }

        std::sort(
            spans.begin(),
            spans.end(),
            [](const FreeSpan& a, const FreeSpan& b) {
                return a.offset < b.offset;
            });

        size_t writeIndex = 0;

        for (size_t readIndex = 1;
            readIndex < spans.size();
            ++readIndex) {
            FreeSpan& merged = spans[writeIndex];
            const FreeSpan& candidate = spans[readIndex];

            const uint64_t mergedEnd =
                static_cast<uint64_t>(merged.offset) + merged.count;
            const uint64_t candidateEnd =
                static_cast<uint64_t>(candidate.offset) + candidate.count;

            // Merge touching or overlapping ranges.
            if (candidate.offset <= mergedEnd) {
                const uint64_t newEnd =
                    std::max(mergedEnd, candidateEnd);

                merged.count = static_cast<uint32_t>(
                    newEnd - merged.offset);
            }
            else {
                ++writeIndex;
                spans[writeIndex] = candidate;
            }
        }

        spans.resize(writeIndex + 1);
        };

    coalesceSpans(m_freeVertexSpans);
    coalesceSpans(m_freeIndexSpans);

    auto vertexIt = std::find_if(
        m_freeVertexSpans.begin(),
        m_freeVertexSpans.end(),
        [vertexCount](const FreeSpan& span) {
            return span.count >= vertexCount;
        });

    auto indexIt = std::find_if(
        m_freeIndexSpans.begin(),
        m_freeIndexSpans.end(),
        [indexCount](const FreeSpan& span) {
            return span.count >= indexCount;
        });

    const uint32_t nextVertex =
        m_nextVertexOffset.load(std::memory_order_relaxed);
    const uint32_t nextIndex =
        m_nextIndexOffset.load(std::memory_order_relaxed);

    // Validate both allocations before changing either allocator state.
    if (vertexIt == m_freeVertexSpans.end()) {
        if (vertexCount > m_maxVertices ||
            nextVertex > m_maxVertices - vertexCount) {
            throw std::runtime_error("Terrain vertex buffer overflow");
        }
    }

    if (indexIt == m_freeIndexSpans.end()) {
        if (indexCount > m_maxIndices ||
            nextIndex > m_maxIndices - indexCount) {
            throw std::runtime_error("Terrain index buffer overflow");
        }
    }

    uint32_t vertexOffset = 0;

    if (vertexIt != m_freeVertexSpans.end()) {
        vertexOffset = vertexIt->offset;

        if (vertexIt->count == vertexCount) {
            m_freeVertexSpans.erase(vertexIt);
        }
        else {
            vertexIt->offset += vertexCount;
            vertexIt->count -= vertexCount;
        }
    }
    else {
        vertexOffset = nextVertex;
        m_nextVertexOffset.store(
            nextVertex + vertexCount,
            std::memory_order_relaxed);
    }

    uint32_t indexOffset = 0;

    if (indexIt != m_freeIndexSpans.end()) {
        indexOffset = indexIt->offset;

        if (indexIt->count == indexCount) {
            m_freeIndexSpans.erase(indexIt);
        }
        else {
            indexIt->offset += indexCount;
            indexIt->count -= indexCount;
        }
    }
    else {
        indexOffset = nextIndex;
        m_nextIndexOffset.store(
            nextIndex + indexCount,
            std::memory_order_relaxed);
    }

    return { vertexOffset, indexOffset };
}

void TerrainRenderer::DeferSpanReturn(const FreeSpan& vertexSpan, const FreeSpan& indexSpan, uint64_t safeFrame) {
    m_pendingSpanReturns.Push({ vertexSpan, indexSpan }, safeFrame);
}

void TerrainRenderer::UploadTerrainChunkAsync(
    TerrainChunkGPU& chunk,
    const std::vector<ModelVertex>& verts,
    const std::vector<uint32_t>& indices)
{
    const VkDeviceSize vertexSize =
        sizeof(ModelVertex) * verts.size();
    const VkDeviceSize indexSize =
        sizeof(uint32_t) * indices.size();

    if (vertexSize == 0 || indexSize == 0) {
        return;
    }

    const auto [vertexOffset, indexOffset] = AllocateSpace(
        static_cast<uint32_t>(verts.size()),
        static_cast<uint32_t>(indices.size()));

    chunk.vertexOffset = vertexOffset;
    chunk.indexOffset = indexOffset;
    chunk.vertexCount = static_cast<uint32_t>(verts.size());
    chunk.indexCount = static_cast<uint32_t>(indices.size());

    std::vector<CopyRegion> regions;
    regions.reserve(2);

    regions.push_back({
        verts.data(),
        vertexSize,
        m_vertexBuffer,
        static_cast<VkDeviceSize>(vertexOffset) * sizeof(ModelVertex)
        });

    regions.push_back({
        indices.data(),
        indexSize,
        m_indexBuffer,
        static_cast<VkDeviceSize>(indexOffset) * sizeof(uint32_t)
        });

    auto chunkPtr = std::make_shared<TerrainChunkGPU>(chunk);
    auto keyPtr = std::make_shared<int64_t>(chunk.key);
    auto cancelledFlag = std::make_shared<bool>(false);

    m_pendingFlags[chunk.key] = cancelledFlag;

    m_uploader->QueueBatchUpload(
        regions,
        [this, chunkPtr, keyPtr, cancelledFlag]() {
            auto pendingIt = m_pendingFlags.find(*keyPtr);

            // Only this upload may remove its own pending-token entry.
            const bool isCurrent =
                pendingIt != m_pendingFlags.end() &&
                pendingIt->second == cancelledFlag;

            if (isCurrent) {
                m_pendingFlags.erase(pendingIt);
            }

            // Old/cancelled uploads must never insert terrain after a newer
            // upload or a chunk removal has replaced their token.
            if (!isCurrent || *cancelledFlag) {
                std::lock_guard<std::mutex> lock(m_terrainAllocMutex);

                m_freeVertexSpans.push_back({
                    chunkPtr->vertexOffset,
                    chunkPtr->vertexCount
                    });

                m_freeIndexSpans.push_back({
                    chunkPtr->indexOffset,
                    chunkPtr->indexCount
                    });

                return;
            }

            auto oldIt = m_terrainChunks.find(*keyPtr);
            if (oldIt != m_terrainChunks.end()) {
                DeferSpanReturn(
                    {
                        oldIt->second.vertexOffset,
                        oldIt->second.vertexCount
                    },
                    {
                        oldIt->second.indexOffset,
                        oldIt->second.indexCount
                    },
                    m_renderer->GetFrameCounter() + m_renderer->GetFramesInFlight());
            }

            chunkPtr->ready = true;
            m_terrainChunks[*keyPtr] = *chunkPtr;
        });
}

float TerrainRenderer::GetCachedHeight(float worldX, float worldZ) {
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

float TerrainRenderer::GetCachedHeightInternal(float worldX, float worldZ) {
    const float GRID = 10.0f;
    int gx = static_cast<int>(std::floor(worldX / GRID + 0.5f));
    int gz = static_cast<int>(std::floor(worldZ / GRID + 0.5f));
    int64_t key = (static_cast<int64_t>(gx) << 32) | (static_cast<uint32_t>(gz));

    // Fast check under lock
    {
        std::lock_guard<std::mutex> lock(m_heightCacheMutex);
        auto it = m_heightCache.find(key);
        if (it != m_heightCache.end() && it->second.valid) {
            return it->second.height;
        }
    }

    float h = Chunk::GetHeight(worldX, worldZ);

    // Re-acquire lock briefly to populate the cache entry
    {
        std::lock_guard<std::mutex> lock(m_heightCacheMutex);
        m_heightCache[key] = { h, true };
    }

    return h;
}

bool TerrainRenderer::IsChunkOccludedInternal(const glm::vec3& chunkCenter, float chunkRadius,
    const glm::vec3& cameraPos) {
    float distToCenter = glm::length(chunkCenter - cameraPos);
    if (distToCenter < chunkRadius * 2.0f) return false;

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

void TerrainRenderer::CreateComputePipeline() {
    VkDescriptorSetLayoutBinding chunkLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutBinding cmdLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutBinding countLayoutBinding{ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutBinding hzbLayoutBinding{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

    std::array<VkDescriptorSetLayoutBinding, 4> bindings = { chunkLayoutBinding, cmdLayoutBinding, countLayoutBinding, hzbLayoutBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, static_cast<uint32_t>(bindings.size()), bindings.data() };
    vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_computeDescriptorSetLayout);

    VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 128};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_computeDescriptorSetLayout, 1, &pushConstantRange };
    vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_computePipelineLayout);

    auto computeShaderCode = VulkanRenderer::ReadFile("shaders/terrain_cull_comp.spv");
    VkShaderModule computeShaderModule = VulkanRenderer::CreateShaderModule(m_device, computeShaderCode);

    VkPipelineShaderStageCreateInfo computeShaderStageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, computeShaderModule, "main" };

    VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, computeShaderStageInfo, m_computePipelineLayout, VK_NULL_HANDLE, 0 };
    vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_computePipeline);

    vkDestroyShaderModule(m_device, computeShaderModule, nullptr);
}

void TerrainRenderer::UpdateComputeDescriptors() {
    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        VkDescriptorBufferInfo chunkInfo{ m_chunkDataBuffers[i], 0, sizeof(TerrainChunkGPUData) * MAX_TERRAIN_CHUNKS };
        VkDescriptorBufferInfo cmdInfo{ m_indirectCommandBuffers[i], 0, sizeof(VkDrawIndexedIndirectCommand) * MAX_TERRAIN_CHUNKS };
        VkDescriptorBufferInfo countInfo{ m_drawCountBuffers[i], 0, sizeof(uint32_t) };

        VkWriteDescriptorSet writes[3]{};
        writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_computeDescriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &chunkInfo, nullptr };
        writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_computeDescriptorSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &cmdInfo, nullptr };
        writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_computeDescriptorSets[i], 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &countInfo, nullptr };

        vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
    }
}

void TerrainRenderer::UpdateHZBDescriptor(VkImageView hzbView, VkSampler hzbSampler) {
    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        VkDescriptorImageInfo hzbInfo{ hzbSampler, hzbView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_computeDescriptorSets[i], 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &hzbInfo, nullptr, nullptr };
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }
}