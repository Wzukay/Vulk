#include "renderer_water.h"

#include <algorithm>
#include <stdexcept>

#include "renderer.h"
#include "settings.h"

void WaterRenderer::Init(VkDevice device, VulkanRenderer* renderer, RingBufferUploader* uploader, VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout globalLayout, VkSampleCountFlagBits msaaSamples) {
    m_device = device;
    m_renderer = renderer;
    m_uploader = uploader;
    m_framesInFlight = std::min(MAX_FRAMES_IN_FLIGHT, m_renderer->GetFramesInFlight());

    m_renderer->CreateBuffer(sizeof(ModelVertex) * MAX_WATER_VERTICES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vertexBuffer, m_vertexMemory);
    m_renderer->CreateBuffer(sizeof(uint32_t) * MAX_WATER_INDICES, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indexBuffer, m_indexMemory);

    for (uint32_t i = 0; i < m_framesInFlight; ++i) {
        m_renderer->CreateBuffer(sizeof(WaterChunkGPUData) * MAX_WATER_CHUNKS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_chunkDataBuffers[i], m_chunkDataMemories[i]);
        vkMapMemory(m_device, m_chunkDataMemories[i], 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&m_chunkDataMappedPtrs[i]));

        m_renderer->CreateBuffer(sizeof(VkDrawIndexedIndirectCommand) * MAX_WATER_CHUNKS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indirectBuffers[i], m_indirectMemories[i]);
        m_renderer->CreateBuffer(sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_drawCountBuffers[i], m_drawCountMemories[i]);
    }

    CreatePipeline(colorFormat, depthFormat, globalLayout, msaaSamples);
    CreateCullPipeline();
    CreateCullDescriptors();
}

void WaterRenderer::Tick(uint64_t currentFrame) {
    m_pendingSpanReturns.Flush(currentFrame, [&](SpanReturn& returned) {
        m_freeVertexSpans.push_back(returned.vertexSpan);
        m_freeIndexSpans.push_back(returned.indexSpan);
        });
}

void WaterRenderer::Cleanup() {
    if (m_device == VK_NULL_HANDLE) return;

    if (m_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

    if (m_cullPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_cullPipeline, nullptr);
    if (m_cullPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_cullPipelineLayout, nullptr);
    if (m_cullDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_device, m_cullDescriptorPool, nullptr);
    if (m_cullDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_cullDescriptorSetLayout, nullptr);

    if (m_depthDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_device, m_depthDescriptorPool, nullptr);
    if (m_depthDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_depthDescriptorSetLayout, nullptr);

    for (uint32_t i = 0; i < m_framesInFlight; ++i) {
        if (m_chunkDataMappedPtrs[i] != nullptr) vkUnmapMemory(m_device, m_chunkDataMemories[i]);

        m_renderer->DestroyBuffer(m_chunkDataBuffers[i], m_chunkDataMemories[i]);
        m_renderer->DestroyBuffer(m_indirectBuffers[i], m_indirectMemories[i]);
        m_renderer->DestroyBuffer(m_drawCountBuffers[i], m_drawCountMemories[i]);
    }

    m_renderer->DestroyBuffer(m_vertexBuffer, m_vertexMemory);
    m_renderer->DestroyBuffer(m_indexBuffer, m_indexMemory);

    m_chunks.clear();
    m_freeVertexSpans.clear();
    m_freeIndexSpans.clear();

    m_pipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_cullPipeline = VK_NULL_HANDLE;
    m_cullPipelineLayout = VK_NULL_HANDLE;
    m_cullDescriptorPool = VK_NULL_HANDLE;
    m_cullDescriptorSetLayout = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
}

void WaterRenderer::AddWaterChunk(int64_t key, const std::vector<ModelVertex>& vertices, const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) return;

    auto existingChunk = m_chunks.find(key);
    if (existingChunk != m_chunks.end()) {
        DeferSpanReturn(existingChunk->second);
        m_chunks.erase(existingChunk);
    }

    auto [vertexOffset, indexOffset] = AllocateSpace(static_cast<uint32_t>(vertices.size()), static_cast<uint32_t>(indices.size()));

    WaterChunk chunk{};
    chunk.vertexOffset = vertexOffset;
    chunk.indexOffset = indexOffset;
    chunk.vertexCount = static_cast<uint32_t>(vertices.size());
    chunk.indexCount = static_cast<uint32_t>(indices.size());

    glm::vec3 minBound = vertices.front().pos;
    glm::vec3 maxBound = vertices.front().pos;

    for (const ModelVertex& vertex : vertices) {
        minBound = glm::min(minBound, vertex.pos);
        maxBound = glm::max(maxBound, vertex.pos);
    }

    chunk.center = (minBound + maxBound) * 0.5f;
    chunk.radius = glm::length(maxBound - chunk.center) + 1.0f;

    std::vector<CopyRegion> regions;
    regions.reserve(2);

    regions.push_back({
        vertices.data(),
        sizeof(ModelVertex) * vertices.size(),
        m_vertexBuffer,
        sizeof(ModelVertex) * vertexOffset
        });

    regions.push_back({
        indices.data(),
        sizeof(uint32_t) * indices.size(),
        m_indexBuffer,
        sizeof(uint32_t) * indexOffset
        });

    m_uploader->QueueBatchUpload(regions);
    m_chunks[key] = chunk;
}

void WaterRenderer::RemoveWaterChunk(int64_t key) {
    auto it = m_chunks.find(key);
    if (it == m_chunks.end()) return;

    DeferSpanReturn(it->second);
    m_chunks.erase(it);
}

void WaterRenderer::SetSceneDepth(VkImageView depthView, VkSampler depthSampler) {
    VkDescriptorImageInfo depthInfo{};
    depthInfo.sampler = depthSampler;
    depthInfo.imageView = depthView;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_depthDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &depthInfo;

    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void WaterRenderer::Cull(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos, const glm::mat4& viewProj, uint32_t frameIndex) {
    m_submittedChunkCount = 0;

    WaterChunkGPUData* chunkData = m_chunkDataMappedPtrs[frameIndex];

    for (const auto& [key, chunk] : m_chunks) {
        if (m_submittedChunkCount >= MAX_WATER_CHUNKS) break;

        chunkData[m_submittedChunkCount] = {
            glm::vec4(chunk.center, chunk.radius),
            chunk.indexCount,
            chunk.indexOffset,
            static_cast<int32_t>(chunk.vertexOffset),
            0
        };

        ++m_submittedChunkCount;
    }

    if (m_submittedChunkCount == 0) return;

    const uint32_t zero = 0;

    vkCmdUpdateBuffer(commandBuffer, m_drawCountBuffers[frameIndex], 0, sizeof(uint32_t), &zero);
    vkCmdFillBuffer(commandBuffer, m_indirectBuffers[frameIndex], 0, sizeof(VkDrawIndexedIndirectCommand) * m_submittedChunkCount, 0);

    VkMemoryBarrier transferBarrier{
        VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        nullptr,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
    };

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &transferBarrier, 0, nullptr, 0, nullptr);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_cullPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_cullPipelineLayout, 0, 1, &m_cullDescriptorSets[frameIndex], 0, nullptr);

    WaterCullPush push{};
    push.viewProj = viewProj;
    push.cameraPos = cameraPos;
    push.maxDistance = std::min(g_Settings.renderDistance, g_Settings.GetFogStart());
    push.chunkCount = m_submittedChunkCount;

    vkCmdPushConstants(commandBuffer, m_cullPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(WaterCullPush), &push);
    vkCmdDispatch(commandBuffer, (m_submittedChunkCount + 63) / 64, 1, 1);

    VkMemoryBarrier drawBarrier{
        VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        nullptr,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_INDIRECT_COMMAND_READ_BIT
    };

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &drawBarrier, 0, nullptr, 0, nullptr);
}

void WaterRenderer::Draw(VkCommandBuffer commandBuffer, VkDescriptorSet globalSet, const glm::vec3& cameraPos, float time, uint32_t frameIndex) {
    if (m_submittedChunkCount == 0) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &globalSet, 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 1, 1, &m_depthDescriptorSet, 0, nullptr);

    WaterPushConstants push{};
    push.cameraPos = cameraPos;
    push.time = time;
    push.renderSize = glm::vec2(static_cast<float>(m_renderer->GetInternalWidth()), static_cast<float>(m_renderer->GetInternalHeight()));
    push.sceneUvScale = m_renderer->GetInternalUvScale();

    vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(WaterPushConstants), &push);

    VkDeviceSize offset = 0;

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer, m_indirectBuffers[frameIndex], 0, m_submittedChunkCount, sizeof(VkDrawIndexedIndirectCommand));
}

void WaterRenderer::CreatePipeline(VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout globalLayout, VkSampleCountFlagBits msaaSamples) {
    VkDescriptorSetLayoutBinding depthBinding{};
    depthBinding.binding = 0;
    depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthBinding.descriptorCount = 1;
    depthBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo depthLayoutInfo{};
    depthLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    depthLayoutInfo.bindingCount = 1;
    depthLayoutInfo.pBindings = &depthBinding;

    if (vkCreateDescriptorSetLayout(m_device, &depthLayoutInfo, nullptr, &m_depthDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water depth descriptor set layout.");
    }

    VkDescriptorPoolSize depthPoolSize{};
    depthPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthPoolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo depthPoolInfo{};
    depthPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    depthPoolInfo.maxSets = 1;
    depthPoolInfo.poolSizeCount = 1;
    depthPoolInfo.pPoolSizes = &depthPoolSize;

    if (vkCreateDescriptorPool(m_device, &depthPoolInfo, nullptr, &m_depthDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water depth descriptor pool.");
    }

    VkDescriptorSetAllocateInfo depthAllocateInfo{};
    depthAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    depthAllocateInfo.descriptorPool = m_depthDescriptorPool;
    depthAllocateInfo.descriptorSetCount = 1;
    depthAllocateInfo.pSetLayouts = &m_depthDescriptorSetLayout;

    if (vkAllocateDescriptorSets(m_device, &depthAllocateInfo, &m_depthDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate water depth descriptor set.");
    }

    VkDescriptorSetLayout setLayouts[] = {
        globalLayout,
        m_depthDescriptorSetLayout
    };

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(WaterPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water pipeline layout.");
    }

    auto vertexCode = VulkanRenderer::ReadFile("shaders/water_vert.spv");
    auto fragmentCode = VulkanRenderer::ReadFile("shaders/water_frag.spv");

    VkShaderModule vertexModule = VulkanRenderer::CreateShaderModule(m_device, vertexCode);
    VkShaderModule fragmentModule = VulkanRenderer::CreateShaderModule(m_device, fragmentCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertexModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule, "main" }
    };

    auto bindingDescription = ModelVertex::getBindingDescription();
    auto attributeDescriptions = ModelVertex::getStaticAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDescription;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInput.pVertexAttributeDescriptions = attributeDescriptions.data();

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_FALSE;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(m_device, fragmentModule, nullptr);
        vkDestroyShaderModule(m_device, vertexModule, nullptr);
        throw std::runtime_error("Failed to create water graphics pipeline.");
    }

    vkDestroyShaderModule(m_device, fragmentModule, nullptr);
    vkDestroyShaderModule(m_device, vertexModule, nullptr);
}

void WaterRenderer::CreateCullPipeline() {
    VkDescriptorSetLayoutBinding bindings[] = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    };

    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 3, bindings };
    vkCreateDescriptorSetLayout(m_device, &descriptorLayoutInfo, nullptr, &m_cullDescriptorSetLayout);

    VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(WaterCullPush) };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_cullDescriptorSetLayout, 1, &pushConstantRange };

    vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_cullPipelineLayout);

    auto shaderCode = VulkanRenderer::ReadFile("shaders/water_cull_comp.spv");
    VkShaderModule shaderModule = VulkanRenderer::CreateShaderModule(m_device, shaderCode);

    VkPipelineShaderStageCreateInfo shaderStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, shaderModule, "main" };
    VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, shaderStage, m_cullPipelineLayout, VK_NULL_HANDLE, 0 };

    vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_cullPipeline);

    vkDestroyShaderModule(m_device, shaderModule, nullptr);
}

void WaterRenderer::CreateCullDescriptors() {
    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * m_framesInFlight };
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, m_framesInFlight, 1, &poolSize };

    vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_cullDescriptorPool);

    std::vector<VkDescriptorSetLayout> layouts(m_framesInFlight, m_cullDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_cullDescriptorPool, m_framesInFlight, layouts.data() };

    vkAllocateDescriptorSets(m_device, &allocateInfo, m_cullDescriptorSets.data());

    UpdateCullDescriptors();
}

void WaterRenderer::UpdateCullDescriptors() {
    for (uint32_t i = 0; i < m_framesInFlight; ++i) {
        VkDescriptorBufferInfo chunkInfo{ m_chunkDataBuffers[i], 0, sizeof(WaterChunkGPUData) * MAX_WATER_CHUNKS };
        VkDescriptorBufferInfo indirectInfo{ m_indirectBuffers[i], 0, sizeof(VkDrawIndexedIndirectCommand) * MAX_WATER_CHUNKS };
        VkDescriptorBufferInfo countInfo{ m_drawCountBuffers[i], 0, sizeof(uint32_t) };

        VkWriteDescriptorSet writes[] = {
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_cullDescriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &chunkInfo, nullptr },
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_cullDescriptorSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &indirectInfo, nullptr },
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_cullDescriptorSets[i], 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &countInfo, nullptr }
        };

        vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
    }
}

std::pair<uint32_t, uint32_t> WaterRenderer::AllocateSpace(uint32_t vertexCount, uint32_t indexCount) {
    auto allocateFromFreeList = [](std::vector<FreeSpan>& spans, uint32_t count, uint32_t& offset) {
        for (auto it = spans.begin(); it != spans.end(); ++it) {
            if (it->count < count) continue;

            offset = it->offset;
            it->offset += count;
            it->count -= count;

            if (it->count == 0) spans.erase(it);

            return true;
        }

        return false;
        };

    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;

    bool reusedVertexSpan = allocateFromFreeList(m_freeVertexSpans, vertexCount, vertexOffset);
    bool reusedIndexSpan = allocateFromFreeList(m_freeIndexSpans, indexCount, indexOffset);

    if (!reusedVertexSpan) {
        vertexOffset = m_nextVertexOffset.fetch_add(vertexCount);

        if (vertexOffset + vertexCount > MAX_WATER_VERTICES) {
            throw std::runtime_error("Water vertex buffer overflow");
        }
    }

    if (!reusedIndexSpan) {
        indexOffset = m_nextIndexOffset.fetch_add(indexCount);

        if (indexOffset + indexCount > MAX_WATER_INDICES) {
            throw std::runtime_error("Water index buffer overflow");
        }
    }

    return { vertexOffset, indexOffset };
}

void WaterRenderer::DeferSpanReturn(const WaterChunk& chunk) {
    SpanReturn returned{};
    returned.vertexSpan = { chunk.vertexOffset, chunk.vertexCount };
    returned.indexSpan = { chunk.indexOffset, chunk.indexCount };

    m_pendingSpanReturns.Push(returned, m_renderer->GetFrameCounter() + m_renderer->GetFramesInFlight());
}