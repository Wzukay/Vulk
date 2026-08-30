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

    // 1. Create Compute Descriptor Pool
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3000 } // Enough for 1000 chunks * 3 buffers each
    };
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, 1000, 1, poolSizes };
    vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_computeDescriptorPool);

    // 2. Create Compute Descriptor Set Layout
    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    bindings[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

    VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 3, bindings };
    vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_computeSetLayout);

    // 3. Create Compute Pipeline Layout
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

    // Clean up Compute resources
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

    for (auto& [key, chunk] : m_grassChunks) {
        m_renderer->DestroyBuffer(chunk.instanceBuffer, chunk.instanceMemory);
        m_renderer->DestroyBuffer(chunk.culledBuffer, chunk.culledMemory);
        m_renderer->DestroyBuffer(chunk.indirectBuffer, chunk.indirectMemory);
    }
    m_grassChunks.clear();
    m_pendingFlags.clear();
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

    VkDeviceSize instanceBufferSize = grassInstances.size() * sizeof(GrassInstance);
    VkDeviceSize indirectBufferSize = sizeof(IndirectCommand);

    // 1. Raw Input Buffer
    m_renderer->CreateBuffer(instanceBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, grassChunk->instanceBuffer, grassChunk->instanceMemory);
    // 2. Compute Culled Buffer
    m_renderer->CreateBuffer(instanceBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, grassChunk->culledBuffer, grassChunk->culledMemory);
    // 3. Indirect Draw Argument Buffer
    m_renderer->CreateBuffer(indirectBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, grassChunk->indirectBuffer, grassChunk->indirectMemory);

    // Setup Compute Descriptor Set
    VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_computeDescriptorPool, 1, &m_computeSetLayout };
    vkAllocateDescriptorSets(m_device, &allocInfo, &grassChunk->computeDescriptorSet);

    VkDescriptorBufferInfo inInfo{ grassChunk->instanceBuffer, 0, instanceBufferSize };
    VkDescriptorBufferInfo outInfo{ grassChunk->culledBuffer, 0, instanceBufferSize };
    VkDescriptorBufferInfo indInfo{ grassChunk->indirectBuffer, 0, indirectBufferSize };

    VkWriteDescriptorSet writes[3]{};
    writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, grassChunk->computeDescriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &inInfo, nullptr };
    writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, grassChunk->computeDescriptorSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &outInfo, nullptr };
    writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, grassChunk->computeDescriptorSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &indInfo, nullptr };
    vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);

    CopyRegion region{ grassInstances.data(), instanceBufferSize, grassChunk->instanceBuffer, 0 };
    auto keyPtr = std::make_shared<int64_t>(key);
    auto cancelledFlag = std::make_shared<bool>(false);
    m_pendingFlags[key] = cancelledFlag;

    m_uploader->QueueBatchUpload({ region }, [this, keyPtr, grassChunk, cancelledFlag]() {
        m_pendingFlags.erase(*keyPtr);

        if (*cancelledFlag) {
            m_renderer->DestroyBuffer(grassChunk->instanceBuffer, grassChunk->instanceMemory);
            m_renderer->DestroyBuffer(grassChunk->culledBuffer, grassChunk->culledMemory);
            m_renderer->DestroyBuffer(grassChunk->indirectBuffer, grassChunk->indirectMemory);
            return;
        }

        auto oldIt = m_grassChunks.find(*keyPtr);
        if (oldIt != m_grassChunks.end()) {
            BufferDeletion del;
            del.buffers.push_back(oldIt->second.instanceBuffer);
            del.memories.push_back(oldIt->second.instanceMemory);
            del.buffers.push_back(oldIt->second.culledBuffer);
            del.memories.push_back(oldIt->second.culledMemory);
            del.buffers.push_back(oldIt->second.indirectBuffer);
            del.memories.push_back(oldIt->second.indirectMemory);
            m_renderer->DeferBufferDeletion(std::move(del));
        }

        m_grassChunks[*keyPtr] = *grassChunk;
        });
}

void GrassRenderer::Tick(uint64_t currentFrame) {
}

void GrassRenderer::Cull(VkCommandBuffer commandBuffer) {
    if (m_grassChunks.empty() || m_computePipeline == VK_NULL_HANDLE) return;

    const float maxGrassDist = 5000;
    const glm::vec3& camPos = m_renderer->GetCameraPosition();

    m_visibleChunksThisFrame.clear();

    // 1. GATHER VISIBLE CHUNKS & RESET COUNTERS
    for (const auto& [key, chunk] : m_grassChunks) {
        if (chunk.instanceCount == 0 || chunk.instanceBuffer == VK_NULL_HANDLE) continue;

        // Coarse CPU Frustum Check per Chunk
        if (!m_renderer->IsSphereInFrustum(chunk.center, chunk.radius)) continue;

        float dist = glm::length(chunk.center - camPos);
        if (dist > maxGrassDist + chunk.radius) continue;

        m_visibleChunksThisFrame.push_back(&chunk);

        // Reset the instanceCount (offset 4, size 4) in the indirect buffer
        vkCmdFillBuffer(commandBuffer, chunk.indirectBuffer, 4, 4, 0);
    }

    if (m_visibleChunksThisFrame.empty()) return;

    // Memory Barrier: Ensure buffer fill is complete before the compute shader begins
    VkMemoryBarrier fillBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &fillBarrier, 0, nullptr, 0, nullptr);

    // 2. COMPUTE CULLING PASS
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);

    for (const auto* chunk : m_visibleChunksThisFrame) {
        float dist = glm::length(chunk->center - camPos);
        uint32_t vertexCount = (dist > maxGrassDist * 0.5f) ? 6 : 12; // LOD Logic

        GrassComputePushConstants cPush{};
        cPush.cameraPos = camPos;
        cPush.maxDist = maxGrassDist;
        cPush.totalInstances = chunk->instanceCount;
        cPush.vertexCount = vertexCount;

        vkCmdPushConstants(commandBuffer, m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GrassComputePushConstants), &cPush);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipelineLayout, 0, 1, &chunk->computeDescriptorSet, 0, nullptr);
        vkCmdDispatch(commandBuffer, (chunk->instanceCount + 255) / 256, 1, 1);
    }

    // Memory Barrier: Ensure compute finishes writing to the culled and indirect buffers before graphics reads them
    VkMemoryBarrier computeBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &computeBarrier, 0, nullptr, 0, nullptr);
}

void GrassRenderer::Draw(VkCommandBuffer commandBuffer, VkDescriptorSet sharedDescriptorSet, uint32_t& outDrawCalls) const {
    if (m_visibleChunksThisFrame.empty() || m_pipeline == VK_NULL_HANDLE) return;

    static auto startTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();

    const float maxGrassDist = 5000;
    const glm::vec3& camPos = m_renderer->GetCameraPosition();

    // 3. GRAPHICS PASS
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

        // Bind the CULLED buffer instead of the original instance buffer
        VkBuffer buffers[] = { chunk->culledBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

        // Execute dynamic draw sizing created directly by the GPU
        vkCmdDrawIndirect(commandBuffer, chunk->indirectBuffer, 0, 1, sizeof(IndirectCommand));
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
        BufferDeletion del;
        del.buffers.push_back(it2->second.instanceBuffer);
        del.memories.push_back(it2->second.instanceMemory);
        del.buffers.push_back(it2->second.culledBuffer);
        del.memories.push_back(it2->second.culledMemory);
        del.buffers.push_back(it2->second.indirectBuffer);
        del.memories.push_back(it2->second.indirectMemory);
        m_renderer->DeferBufferDeletion(std::move(del));
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

// ---- Private helpers ----

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

    // --- VULKAN 1.3 DYNAMIC RENDERING ---
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