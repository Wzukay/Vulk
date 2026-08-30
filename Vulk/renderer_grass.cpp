#include "renderer_grass.h"
#include "renderer.h"          // VulkanRenderer methods
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

    for (auto& [key, chunk] : m_grassChunks) {
        m_renderer->DestroyBuffer(chunk.instanceBuffer, chunk.instanceMemory);
    }
    m_grassChunks.clear();

    m_pendingFlags.clear();
}

void GrassRenderer::AddGrass(int64_t key, const std::vector<GrassInstance>& grassInstances) {
    if (grassInstances.empty()) return;

    // Cancel any existing upload for this key
    auto it = m_pendingFlags.find(key);
    if (it != m_pendingFlags.end()) {
        *it->second = true; // mark cancelled
        m_pendingFlags.erase(it);
    }

    // Build payload (same as before)
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

    VkDeviceSize bufferSize = grassInstances.size() * sizeof(GrassInstance);

    VkBuffer instanceBuffer;
    VkDeviceMemory instanceMemory;
    m_renderer->CreateBuffer(bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        instanceBuffer, instanceMemory);
    grassChunk->instanceBuffer = instanceBuffer;
    grassChunk->instanceMemory = instanceMemory;

    // Queue upload
    CopyRegion region{ grassInstances.data(), bufferSize, instanceBuffer, 0 };
    auto keyPtr = std::make_shared<int64_t>(key);
    auto cancelledFlag = std::make_shared<bool>(false);

    // Store the cancellation flag
    m_pendingFlags[key] = cancelledFlag;

    m_uploader->QueueBatchUpload({ region }, [this, keyPtr, grassChunk, cancelledFlag]() {
        // Remove from map
        m_pendingFlags.erase(*keyPtr);

        if (*cancelledFlag) {
            // Upload cancelled; discard buffer
            m_renderer->DestroyBuffer(grassChunk->instanceBuffer, grassChunk->instanceMemory);
            return;
        }

        // Replace old chunk if exists
        auto oldIt = m_grassChunks.find(*keyPtr);
        if (oldIt != m_grassChunks.end()) {
            BufferDeletion del;
            del.buffers.push_back(oldIt->second.instanceBuffer);
            del.memories.push_back(oldIt->second.instanceMemory);
            m_renderer->DeferBufferDeletion(std::move(del));
        }

        m_grassChunks[*keyPtr] = *grassChunk;
        });
}

void GrassRenderer::Tick(uint64_t currentFrame) {

}

void GrassRenderer::Draw(VkCommandBuffer commandBuffer, VkDescriptorSet sharedDescriptorSet, uint32_t& outDrawCalls) const {
    if (m_grassChunks.empty() || m_pipeline == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout, 0, 1, &sharedDescriptorSet, 0, nullptr);

    // Time for animations
    static auto startTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(
        std::chrono::high_resolution_clock::now() - startTime).count();

    const float maxGrassDist = 5000;
    const glm::vec3& camPos = m_renderer->GetCameraPosition();

    for (const auto& [key, chunk] : m_grassChunks) {
        if (chunk.instanceCount == 0 || chunk.instanceBuffer == VK_NULL_HANDLE) continue;
        float dist = glm::length(chunk.center - camPos);

        if (!m_renderer->IsSphereInFrustum(chunk.center, chunk.radius)) continue;

        float lodStart = maxGrassDist * 0.75f;
        float lodFactor = std::clamp((dist - lodStart) / (maxGrassDist - lodStart), 0.0f, 1.0f);

        uint32_t vertexCount = 12;
        if (dist > maxGrassDist * 0.5f) {
            vertexCount = 6;
        }

        // Push constants
        GrassPushConstants push{};
        push.time = time;
        push.textureId = 3;
        push.windStrength = 1.7f;
        push.windSpeed = 2.5f;
        push.lodFactor = lodFactor;

        vkCmdPushConstants(commandBuffer, m_pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(GrassPushConstants), &push);

        // Bind instance buffer and draw
        VkBuffer buffers[] = { chunk.instanceBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

        vkCmdDraw(commandBuffer, vertexCount, chunk.instanceCount, 0, 0);

        outDrawCalls++;
    }
}

bool GrassRenderer::HasChunk(int64_t key) const {
    return m_grassChunks.find(key) != m_grassChunks.end();
}

void GrassRenderer::RemoveChunk(int64_t key) {
    // Cancel any pending upload for this key
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