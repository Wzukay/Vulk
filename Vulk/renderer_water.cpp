#include "renderer_water.h"
#include "asset_manager.h"
#include "renderer.h"          // VulkanRenderer methods

#include <stdexcept>
#include <iostream>

void WaterRenderer::Init(VkDevice device, VkRenderPass renderPass,
    VkDescriptorSetLayout sharedSetLayout,
    VkSampleCountFlagBits msaaSamples,
    VulkanRenderer* renderer) {
    m_device = device;
    m_renderer = renderer;
    m_startTime = std::chrono::high_resolution_clock::now();
    CreatePipeline(renderPass, sharedSetLayout, msaaSamples);
}

void WaterRenderer::Cleanup(VkDevice device) {
    if (device == VK_NULL_HANDLE) return;

    // Destroy pipeline resources
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_vertModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, m_vertModule, nullptr);
        m_vertModule = VK_NULL_HANDLE;
    }
    if (m_fragModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, m_fragModule, nullptr);
        m_fragModule = VK_NULL_HANDLE;
    }

    // Delete all water bodies immediately (they are not in flight any more)
    for (auto& body : m_waterBodies) {
        m_renderer->DestroyBuffer(body.vertexBuffer, body.vertexMemory);
        m_renderer->DestroyBuffer(body.indexBuffer, body.indexMemory);
    }
    m_waterBodies.clear();
    m_waterBodyLookup.clear();
}

void WaterRenderer::AddWaterBodyForChunk(int64_t chunkKey, const WaterMesh& mesh,
    const std::string& normalMapPath,
    float tiling, float waveStrength) {
    if (mesh.vertices.empty() || mesh.indices.empty()) return;

    // Remove existing water for this chunk if any
    auto it = m_waterBodyLookup.find(chunkKey);
    if (it != m_waterBodyLookup.end()) {
        RemoveWaterBody(chunkKey);
    }

    size_t idx = CreateWaterBodyGPU(mesh, normalMapPath, tiling, waveStrength);
    m_waterBodyLookup[chunkKey] = idx;
}

void WaterRenderer::AddWaterBody(const WaterMesh& mesh, const std::string& normalMapPath,
    float tiling, float waveStrength) {
    if (mesh.vertices.empty() || mesh.indices.empty()) return;
    CreateWaterBodyGPU(mesh, normalMapPath, tiling, waveStrength);
}

void WaterRenderer::RemoveWaterBody(int64_t chunkKey) {
    auto it = m_waterBodyLookup.find(chunkKey);
    if (it == m_waterBodyLookup.end()) return;

    size_t targetIndex = it->second;
    if (targetIndex >= m_waterBodies.size()) {
        m_waterBodyLookup.erase(it);
        return;
    }

    WaterBodyGPU& body = m_waterBodies[targetIndex];

    // Enqueue buffer deletion (renderer will defer by MAX_FRAMES_IN_FLIGHT)
    BufferDeletion del;
    del.buffers.push_back(body.vertexBuffer);
    del.memories.push_back(body.vertexMemory);
    del.buffers.push_back(body.indexBuffer);
    del.memories.push_back(body.indexMemory);
    m_renderer->DeferBufferDeletion(std::move(del));

    // Swap‑and‑pop removal
    if (targetIndex != m_waterBodies.size() - 1) {
        m_waterBodies[targetIndex] = m_waterBodies.back();
        // Update lookup for the moved entry
        for (auto& pair : m_waterBodyLookup) {
            if (pair.second == m_waterBodies.size() - 1) {
                pair.second = targetIndex;
                break;
            }
        }
    }
    m_waterBodies.pop_back();
    m_waterBodyLookup.erase(it);
}

void WaterRenderer::Draw(VkCommandBuffer commandBuffer, VkDescriptorSet sharedDescriptorSet, uint32_t& outDrawCalls) const {
    if (m_waterBodies.empty() || m_pipeline == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout, 0, 1, &sharedDescriptorSet, 0, nullptr);

    float elapsed = std::chrono::duration<float>(
        std::chrono::high_resolution_clock::now() - m_startTime).count();

    for (const auto& body : m_waterBodies) {
        VkBuffer vertexBuffers[] = { body.vertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, body.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        WaterPushConstants constants{};
        constants.modelMatrix = glm::mat4(1.0f);
        constants.time = elapsed;
        constants.normalTextureId = body.normalTextureId;
        constants.tiling = body.tiling;
        constants.waveStrength = body.waveStrength;

        vkCmdPushConstants(commandBuffer, m_pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(WaterPushConstants), &constants);

        vkCmdDrawIndexed(commandBuffer, body.indexCount, 1, 0, 0, 0);
        
        outDrawCalls++;
    }
}

// ---- Private helpers ----

void WaterRenderer::CreatePipeline(VkRenderPass renderPass,
    VkDescriptorSetLayout sharedSetLayout,
    VkSampleCountFlagBits msaaSamples) {
    // Load shaders (these paths are the same as before)
    auto vertCode = VulkanRenderer::ReadFile("shaders/water_vert.spv");
    auto fragCode = VulkanRenderer::ReadFile("shaders/water_frag.spv");

    VkShaderModule vertModule = VulkanRenderer::CreateShaderModule(m_device, vertCode);
    VkShaderModule fragModule = VulkanRenderer::CreateShaderModule(m_device, fragCode);
    m_vertModule = vertModule;
    m_fragModule = fragModule;

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

    // Vertex input: WaterVertex { pos, uv }
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(WaterVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributes{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(WaterVertex, pos);

    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = offsetof(WaterVertex, uv);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer: no culling, fill mode
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.sampleShadingEnable = VK_FALSE;
    multisample.rasterizationSamples = msaaSamples;

    // Depth test ON, depth write OFF (translucent)
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Alpha blending for water
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(WaterPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &sharedSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water pipeline layout");
    }

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
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water pipeline");
    }
}

size_t WaterRenderer::CreateWaterBodyGPU(const WaterMesh& mesh,
    const std::string& normalMapPath,
    float tiling, float waveStrength) {
    WaterBodyGPU body{};
    body.indexCount = static_cast<uint32_t>(mesh.indices.size());
    body.normalTextureId = g_AssetManager.GetNormalTextureId(normalMapPath);
    body.tiling = tiling;
    body.waveStrength = waveStrength;

    VkDeviceSize vertexSize = sizeof(WaterVertex) * mesh.vertices.size();
    VkDeviceSize indexSize = sizeof(uint32_t) * mesh.indices.size();

    // Use renderer's buffer creation (they are device-local or host-visible as needed)
    m_renderer->CreateBuffer(vertexSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        body.vertexBuffer, body.vertexMemory);

    void* data;
    vkMapMemory(m_device, body.vertexMemory, 0, vertexSize, 0, &data);
    memcpy(data, mesh.vertices.data(), vertexSize);
    vkUnmapMemory(m_device, body.vertexMemory);

    m_renderer->CreateBuffer(indexSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        body.indexBuffer, body.indexMemory);

    vkMapMemory(m_device, body.indexMemory, 0, indexSize, 0, &data);
    memcpy(data, mesh.indices.data(), indexSize);
    vkUnmapMemory(m_device, body.indexMemory);

    m_waterBodies.push_back(body);
    return m_waterBodies.size() - 1;
}