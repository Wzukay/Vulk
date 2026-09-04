#include "renderer_water.h"
#include "renderer.h"
#include <stdexcept>

void WaterRenderer::Init(VkDevice device, VulkanRenderer* renderer, RingBufferUploader* uploader, VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout globalLayout, VkSampleCountFlagBits msaaSamples) {
    m_device = device;
    m_renderer = renderer;
    m_uploader = uploader;

    CreatePipeline(colorFormat, depthFormat, globalLayout, msaaSamples);
}

void WaterRenderer::AddWaterChunk(int64_t key, const std::vector<ModelVertex>& vertices, const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) return;

    auto it = m_chunks.find(key);
    if (it != m_chunks.end()) {
        BufferDeletion deletion;
        deletion.buffers = { it->second.vertexBuffer, it->second.indexBuffer };
        deletion.memories = { it->second.vertexMemory, it->second.indexMemory };
        m_renderer->DeferBufferDeletion(std::move(deletion));
    }

    WaterChunk chunk;
    chunk.indexCount = static_cast<uint32_t>(indices.size());

    VkDeviceSize vSize = vertices.size() * sizeof(ModelVertex);
    VkDeviceSize iSize = indices.size() * sizeof(uint32_t);

    m_renderer->CreateBuffer(vSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, chunk.vertexBuffer, chunk.vertexMemory);
    m_renderer->CreateBuffer(iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, chunk.indexBuffer, chunk.indexMemory);

    m_uploader->QueueBatchUpload({
        { vertices.data(), vSize, chunk.vertexBuffer, 0 },
        { indices.data(), iSize, chunk.indexBuffer, 0 }
        }, nullptr);

    m_chunks[key] = chunk;
}

void WaterRenderer::RemoveWaterChunk(int64_t key) {
    auto it = m_chunks.find(key);
    if (it != m_chunks.end()) {
        BufferDeletion deletion;
        deletion.buffers = { it->second.vertexBuffer, it->second.indexBuffer };
        deletion.memories = { it->second.vertexMemory, it->second.indexMemory };

        m_renderer->DeferBufferDeletion(std::move(deletion));
        m_chunks.erase(it);
    }
}

void WaterRenderer::CreatePipeline(VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout globalLayout, VkSampleCountFlagBits msaaSamples) {
    VkPushConstantRange pcRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(WaterPushConstants) };
    VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &globalLayout, 1, &pcRange };
    vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout);

    auto vertCode = VulkanRenderer::ReadFile("shaders/water_vert.spv");
    auto fragCode = VulkanRenderer::ReadFile("shaders/water_frag.spv");
    VkShaderModule vMod = VulkanRenderer::CreateShaderModule(m_device, vertCode);
    VkShaderModule fMod = VulkanRenderer::CreateShaderModule(m_device, fragCode);

    VkPipelineShaderStageCreateInfo stages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vMod, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fMod, "main" }
    };

    // Update vertex input to match the standard ModelVertex layout
    auto bindingDescription = ModelVertex::getBindingDescription();
    auto attributeDescriptions = ModelVertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertInput.vertexBindingDescriptionCount = 1;
    vertInput.pVertexBindingDescriptions = &bindingDescription;
    vertInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertInput.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAsm{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
    VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };
    VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f };
    VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, msaaSamples, VK_FALSE, 1.0f };

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.blendEnable = VK_TRUE;
    blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAtt.colorBlendOp = VK_BLEND_OP_ADD;
    blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAtt.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAtt.colorWriteMask = 0xF;

    VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &blendAtt };

    VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dynStates };

    VkPipelineRenderingCreateInfo renderInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, nullptr, 0, 1, &colorFormat, depthFormat, VK_FORMAT_UNDEFINED };

    VkGraphicsPipelineCreateInfo pInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &renderInfo, 0, 2, stages, &vertInput, &inputAsm, nullptr, &viewportState, &rasterizer, &multisampling, &depthStencil, &colorBlending, &dynamicState, m_pipelineLayout, VK_NULL_HANDLE, 0 };
    vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &m_pipeline);

    vkDestroyShaderModule(m_device, fMod, nullptr);
    vkDestroyShaderModule(m_device, vMod, nullptr);
}

void WaterRenderer::Cleanup() {
    if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

    for (auto& pair : m_chunks) {
        m_renderer->DestroyBuffer(pair.second.vertexBuffer, pair.second.vertexMemory);
        m_renderer->DestroyBuffer(pair.second.indexBuffer, pair.second.indexMemory);
    }
    m_chunks.clear();
}

void WaterRenderer::Draw(VkCommandBuffer cmd, VkDescriptorSet globalSet, const glm::vec3& camPos, float time) {
    if (m_chunks.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &globalSet, 0, nullptr);

    WaterPushConstants pc{};
    pc.cameraPos = camPos;
    pc.time = time;
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(WaterPushConstants), &pc);

    for (const auto& pair : m_chunks) {
        const WaterChunk& chunk = pair.second;
        VkBuffer vBuffers[] = { chunk.vertexBuffer };
        VkDeviceSize offsets[] = { 0 };

        vkCmdBindVertexBuffers(cmd, 0, 1, vBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, chunk.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, chunk.indexCount, 1, 0, 0, 0);
    }
}