#include "renderer_grass.h"
#include "renderer.h"
#include "asset_manager.h"
#include "chunk.h"

#include <stdexcept>
#include <iostream>
#include <cassert>

void GrassRenderer::Init(VkDevice device, VulkanRenderer* renderer,
    RingBufferUploader* uploader,
    VkFormat colorFormat, VkFormat depthFormat,
    VkDescriptorSetLayout sharedSetLayout,
    VkSampleCountFlagBits msaaSamples) {
    m_device = device;
    m_renderer = renderer;
    m_uploader = uploader;

    // 1. Create Suballocator Buffers
    VkDeviceSize instanceBufferSize = m_maxInstances * sizeof(GrassInstance);
    VkDeviceSize indirectBufferSize = m_maxIndirect * sizeof(VkDrawIndirectCommand);

    m_renderer->CreateBuffer(instanceBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_instanceBuffer, m_instanceMemory);

    for (int i = 0; i < 3; ++i) {
        m_renderer->CreateBuffer(instanceBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_culledBuffers[i], m_culledMemories[i]);
        m_renderer->CreateBuffer(indirectBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indirectBuffers[i], m_indirectMemories[i]);
    }

    // 2. Create Compute Descriptor Pool
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6000 }
    };
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, 2000, 1, poolSizes };
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_computeDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create grass compute descriptor pool");
    }

    // 3. Create Compute Descriptor Set Layout
    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    bindings[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

    VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 3, bindings };
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_computeSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create grass compute layout");
    }

    VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GrassComputePushConstants) };
    VkPipelineLayoutCreateInfo pLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_computeSetLayout, 1, &pcRange };
    vkCreatePipelineLayout(m_device, &pLayoutInfo, nullptr, &m_computePipelineLayout);

    CreateComputePipeline();
    CreatePipeline(colorFormat, depthFormat, sharedSetLayout, msaaSamples);
}

void GrassRenderer::Cleanup() {
    if (m_device == VK_NULL_HANDLE) return;

    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    if (m_computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_computePipeline, nullptr);
        m_computePipeline = VK_NULL_HANDLE;
    }
    if (m_computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_computePipelineLayout, nullptr);
        m_computePipelineLayout = VK_NULL_HANDLE;
    }
    if (m_computeSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_computeSetLayout, nullptr);
        m_computeSetLayout = VK_NULL_HANDLE;
    }
    if (m_computeDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_computeDescriptorPool, nullptr);
        m_computeDescriptorPool = VK_NULL_HANDLE;
    }

    m_renderer->DestroyBuffer(m_instanceBuffer, m_instanceMemory);
    for (int i = 0; i < 3; ++i) {
        m_renderer->DestroyBuffer(m_culledBuffers[i], m_culledMemories[i]);
        m_renderer->DestroyBuffer(m_indirectBuffers[i], m_indirectMemories[i]);
    }

    m_grassChunks.clear();
    m_freeInstanceSpans.clear();
    m_freeIndirectSpans.clear();
    m_garbageSets.clear();
    m_pendingFlags.clear();
}

std::pair<uint32_t, uint32_t> GrassRenderer::AllocateSpace(uint32_t instanceCount, uint32_t indirectCount) {
    std::lock_guard<std::mutex> lock(m_allocMutex);

    // Hard alignment to 256 bytes to satisfy strict Vulkan offset limits
    uint32_t instAligned = (instanceCount + 7) & ~7;   // 32 bytes * 8 = 256
    uint32_t indAligned = (indirectCount + 15) & ~15;  // 16 bytes * 16 = 256

    uint32_t instOffset = 0;
    auto instIt = std::find_if(m_freeInstanceSpans.begin(), m_freeInstanceSpans.end(),
        [&](const FreeSpan& s) { return s.count >= instAligned; });
    if (instIt != m_freeInstanceSpans.end()) {
        instOffset = instIt->offset;
        if (instIt->count == instAligned) m_freeInstanceSpans.erase(instIt);
        else { instIt->offset += instAligned; instIt->count -= instAligned; }
    }
    else {
        instOffset = m_nextInstanceOffset.fetch_add(instAligned);
        if (instOffset + instAligned > m_maxInstances) throw std::runtime_error("Grass instance buffer overflow");
    }

    uint32_t indOffset = 0;
    auto indIt = std::find_if(m_freeIndirectSpans.begin(), m_freeIndirectSpans.end(),
        [&](const FreeSpan& s) { return s.count >= indAligned; });
    if (indIt != m_freeIndirectSpans.end()) {
        indOffset = indIt->offset;
        if (indIt->count == indAligned) m_freeIndirectSpans.erase(indIt);
        else { indIt->offset += indAligned; indIt->count -= indAligned; }
    }
    else {
        indOffset = m_nextIndirectOffset.fetch_add(indAligned);
        if (indOffset + indAligned > m_maxIndirect) throw std::runtime_error("Grass indirect buffer overflow");
    }

    return { instOffset, indOffset };
}

void GrassRenderer::DeferSpanReturn(const FreeSpan& instSpan, const FreeSpan& indSpan, uint64_t safeFrame) {
    m_pendingSpanReturns.Push({ instSpan, indSpan }, safeFrame);
}

void GrassRenderer::Tick(uint64_t currentFrame) {
    m_pendingSpanReturns.Flush(currentFrame, [&](SpanReturn& ret) {
        std::lock_guard<std::mutex> lock(m_allocMutex);
        m_freeInstanceSpans.push_back(ret.instanceSpan);
        m_freeIndirectSpans.push_back(ret.indirectSpan);
        });

    for (auto it = m_garbageSets.begin(); it != m_garbageSets.end(); ) {
        if (currentFrame >= it->safeFrame) {
            vkFreeDescriptorSets(m_device, m_computeDescriptorPool, 3, it->sets.data());
            it = m_garbageSets.erase(it);
        }
        else {
            ++it;
        }
    }
}

void GrassRenderer::AddGrass(int64_t key, const std::vector<GrassInstance>& grassInstances) {
    if (grassInstances.empty()) return;

    auto it = m_pendingFlags.find(key);
    if (it != m_pendingFlags.end()) {
        *it->second = true;
        m_pendingFlags.erase(it);
    }

    auto grassChunk = std::make_shared<GrassChunkGPU>();
    grassChunk->instanceCount = static_cast<uint32_t>(grassInstances.size());

    glm::vec3 minP(FLT_MAX), maxP(-FLT_MAX);
    for (const auto& inst : grassInstances) {
        minP = glm::min(minP, inst.position);
        maxP = glm::max(maxP, inst.position);
    }
    grassChunk->center = (minP + maxP) * 0.5f;
    float r2 = 0.0f;
    for (const auto& inst : grassInstances) {
        float d2 = glm::length2(inst.position - grassChunk->center);
        if (d2 > r2) r2 = d2;
    }
    grassChunk->radius = sqrt(r2) + 0.5f;

    auto [instOff, indOff] = AllocateSpace(grassChunk->instanceCount, 1);
    grassChunk->instanceOffset = instOff;
    grassChunk->indirectOffset = indOff;

    VkDeviceSize instBytes = grassChunk->instanceCount * sizeof(GrassInstance);
    VkDeviceSize indBytes = sizeof(VkDrawIndirectCommand);

    for (int i = 0; i < 3; ++i) {
        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_computeDescriptorPool, 1, &m_computeSetLayout };
        if (vkAllocateDescriptorSets(m_device, &allocInfo, &grassChunk->computeDescriptorSets[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate grass descriptor sets");
        }

        // Alignments automatically conform to the 256-byte constraint
        VkDescriptorBufferInfo inInfo{ m_instanceBuffer, instOff * sizeof(GrassInstance), instBytes };
        VkDescriptorBufferInfo outInfo{ m_culledBuffers[i], instOff * sizeof(GrassInstance), instBytes };
        VkDescriptorBufferInfo indInfo{ m_indirectBuffers[i], indOff * sizeof(VkDrawIndirectCommand), indBytes };

        VkWriteDescriptorSet writes[3]{};
        writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, grassChunk->computeDescriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &inInfo, nullptr };
        writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, grassChunk->computeDescriptorSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &outInfo, nullptr };
        writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, grassChunk->computeDescriptorSets[i], 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &indInfo, nullptr };
        vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
    }

    CopyRegion region{ grassInstances.data(), instBytes, m_instanceBuffer, instOff * sizeof(GrassInstance) };
    auto keyPtr = std::make_shared<int64_t>(key);
    auto cancelledFlag = std::make_shared<bool>(false);
    m_pendingFlags[key] = cancelledFlag;

    m_uploader->QueueBatchUpload({ region }, [this, keyPtr, grassChunk, cancelledFlag]() {
        m_pendingFlags.erase(*keyPtr);

        if (*cancelledFlag) {
            uint32_t instAligned = (grassChunk->instanceCount + 7) & ~7;
            uint32_t indAligned = (1 + 15) & ~15;
            DeferSpanReturn({ grassChunk->instanceOffset, instAligned }, { grassChunk->indirectOffset, indAligned }, m_renderer->GetFrameCounter() + 3);

            GrassGarbage gc{};
            gc.sets = grassChunk->computeDescriptorSets;
            gc.safeFrame = m_renderer->GetFrameCounter() + 3;
            m_garbageSets.push_back(gc);
            return;
        }

        auto oldIt = m_grassChunks.find(*keyPtr);
        if (oldIt != m_grassChunks.end()) {
            uint32_t instAligned = (oldIt->second.instanceCount + 7) & ~7;
            uint32_t indAligned = (1 + 15) & ~15;
            DeferSpanReturn({ oldIt->second.instanceOffset, instAligned }, { oldIt->second.indirectOffset, indAligned }, m_renderer->GetFrameCounter() + 3);

            GrassGarbage gc{};
            gc.sets = oldIt->second.computeDescriptorSets;
            gc.safeFrame = m_renderer->GetFrameCounter() + 3;
            m_garbageSets.push_back(gc);

            m_grassChunks.erase(oldIt);
        }

        m_grassChunks[*keyPtr] = *grassChunk;
        });
}

void GrassRenderer::Cull(VkCommandBuffer commandBuffer, uint32_t currentFrameIndex) {
    if (m_grassChunks.empty() || m_computePipeline == VK_NULL_HANDLE) return;

    const float maxGrassDist = 5000;
    const glm::vec3& camPos = m_renderer->GetCameraPosition();

    m_visibleChunksThisFrame.clear();

    for (const auto& [key, chunk] : m_grassChunks) {
        if (chunk.instanceCount == 0) continue;
        if (!m_renderer->IsSphereInFrustum(chunk.center, chunk.radius)) continue;

        float dist = glm::length(chunk.center - camPos);
        if (dist > maxGrassDist + chunk.radius) continue;

        m_visibleChunksThisFrame.push_back(&chunk);

        // Reset the instanceCount (offset + 4) inside the chunk's allocated block
        vkCmdFillBuffer(commandBuffer, m_indirectBuffers[currentFrameIndex], chunk.indirectOffset * sizeof(VkDrawIndirectCommand) + 4, 4, 0);
    }

    if (m_visibleChunksThisFrame.empty()) return;

    VkMemoryBarrier fillBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &fillBarrier, 0, nullptr, 0, nullptr);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);

    for (const auto* chunk : m_visibleChunksThisFrame) {
        GrassComputePushConstants cPush{};
        cPush.cameraPos = camPos;
        cPush.maxDist = maxGrassDist;
        cPush.totalInstances = chunk->instanceCount;
        cPush.vertexCount = 9;

        const auto& planes = m_renderer->GetFrustumPlanes();
        for (int i = 0; i < 6; ++i) cPush.frustumPlanes[i] = glm::vec4(planes[i].normal, planes[i].distance);

        vkCmdPushConstants(commandBuffer, m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GrassComputePushConstants), &cPush);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipelineLayout, 0, 1, &chunk->computeDescriptorSets[currentFrameIndex], 0, nullptr);
        vkCmdDispatch(commandBuffer, (chunk->instanceCount + 255) / 256, 1, 1);
    }

    VkMemoryBarrier computeBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &computeBarrier, 0, nullptr, 0, nullptr);
}

void GrassRenderer::Draw(VkCommandBuffer commandBuffer, VkDescriptorSet sharedDescriptorSet, uint32_t currentFrameIndex, uint32_t& outDrawCalls) const {
    if (m_visibleChunksThisFrame.empty() || m_pipeline == VK_NULL_HANDLE) return;

    static auto startTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();

    const float maxGrassDist = 5000;
    const glm::vec3& camPos = m_renderer->GetCameraPosition();

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &sharedDescriptorSet, 0, nullptr);

    for (const auto* chunk : m_visibleChunksThisFrame) {
        float dist = glm::length(chunk->center - camPos);
        float lodStart = maxGrassDist * 0.75f;
        float lodFactor = std::clamp((dist - lodStart) / (maxGrassDist - lodStart), 0.0f, 1.0f);

        GrassPushConstants push{};
        push.time = time;
        push.textureId = 3;
        push.windStrength = 1.7f;
        push.windSpeed = 2.5f;
        push.lodFactor = lodFactor;

        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GrassPushConstants), &push);

        VkBuffer buffers[] = { m_culledBuffers[currentFrameIndex] };
        VkDeviceSize offsets[] = { chunk->instanceOffset * sizeof(GrassInstance) };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

        vkCmdDrawIndirect(commandBuffer, m_indirectBuffers[currentFrameIndex], chunk->indirectOffset * sizeof(VkDrawIndirectCommand), 1, sizeof(VkDrawIndirectCommand));
        outDrawCalls++;
    }
}

bool GrassRenderer::HasChunk(int64_t key) const {
    return m_grassChunks.find(key) != m_grassChunks.end();
}

void GrassRenderer::RemoveChunk(int64_t key) {
    auto it = m_pendingFlags.find(key);
    if (it != m_pendingFlags.end()) {
        *it->second = true;
        m_pendingFlags.erase(it);
    }

    auto it2 = m_grassChunks.find(key);
    if (it2 != m_grassChunks.end()) {
        uint32_t instAligned = (it2->second.instanceCount + 7) & ~7;
        uint32_t indAligned = (1 + 15) & ~15;
        DeferSpanReturn({ it2->second.instanceOffset, instAligned }, { it2->second.indirectOffset, indAligned }, m_renderer->GetFrameCounter() + 3);

        GrassGarbage gc{};
        gc.sets = it2->second.computeDescriptorSets;
        gc.safeFrame = m_renderer->GetFrameCounter() + 3;
        m_garbageSets.push_back(gc);

        m_grassChunks.erase(it2);
    }
}

void GrassRenderer::CancelPendingUpload(int64_t key) {
    auto it = m_pendingFlags.find(key);
    if (it != m_pendingFlags.end()) {
        *it->second = true;
        m_pendingFlags.erase(it);
    }
}

void GrassRenderer::CreateComputePipeline() {
    auto compCode = VulkanRenderer::ReadFile("shaders/grass_cull_comp.spv");
    VkShaderModule compModule = VulkanRenderer::CreateShaderModule(m_device, compCode);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = compModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_computePipelineLayout;

    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_computePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create grass compute pipeline");
    }

    vkDestroyShaderModule(m_device, compModule, nullptr);
}

void GrassRenderer::CreatePipeline(VkFormat colorFormat, VkFormat depthFormat,
    VkDescriptorSetLayout sharedSetLayout,
    VkSampleCountFlagBits msaaSamples) {
    auto vertCode = VulkanRenderer::ReadFile("shaders/grass_vert.spv");
    auto fragCode = VulkanRenderer::ReadFile("shaders/grass_frag.spv");

    VkShaderModule vertModule = VulkanRenderer::CreateShaderModule(m_device, vertCode);
    VkShaderModule fragModule = VulkanRenderer::CreateShaderModule(m_device, fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(GrassInstance);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    std::array<VkVertexInputAttributeDescription, 4> attribs{};
    attribs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GrassInstance, position) };
    attribs[1] = { 1, 0, VK_FORMAT_R32_SFLOAT,       offsetof(GrassInstance, rotation) };
    attribs[2] = { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GrassInstance, scale) };
    attribs[3] = { 3, 0, VK_FORMAT_R32_SFLOAT,       offsetof(GrassInstance, windOffset) };

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
    vertexInput.pVertexAttributeDescriptions = attribs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = msaaSamples;
    multisample.sampleShadingEnable = VK_FALSE;
    multisample.alphaToCoverageEnable = VK_TRUE;
    multisample.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(GrassPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &sharedSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create grass pipeline layout");
    }

    VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
    pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipelineRenderingCreateInfo.colorAttachmentCount = 1;
    pipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
    pipelineRenderingCreateInfo.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;

    pipelineInfo.pNext = &pipelineRenderingCreateInfo;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create grass pipeline");
    }

    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
}