#include "renderer_boid.h"
#include "renderer.h"
#include "ring_buffer_uploader.h"

#include <stdexcept>
#include <algorithm>

void BoidRenderer::Init(VkDevice device, VulkanRenderer* renderer, RingBufferUploader* uploader, VkFormat colorFormat, VkFormat depthFormat,
    VkDescriptorSetLayout sharedSetLayout, VkSampleCountFlagBits msaaSamples)
{
    m_device = device;
    m_renderer = renderer;
    m_uploader = uploader;
    m_framesInFlight = std::min(m_renderer->GetFramesInFlight(), MAX_FRAMES_IN_FLIGHT);

    VkDeviceSize bufferSize = m_maxBoids * sizeof(BoidInstance);

    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        m_renderer->CreateBuffer(bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_boidBuffers[i], m_boidMemories[i]);
    }

    CreateComputePipeline();
    CreateGraphicsPipeline(colorFormat, depthFormat, sharedSetLayout, msaaSamples);
}

void BoidRenderer::Cleanup() {
    if (m_device == VK_NULL_HANDLE) return;

    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        m_renderer->DestroyBuffer(m_boidBuffers[i], m_boidMemories[i]);
    }

    for (auto& [key, token] : m_pendingUploads) {
        token->store(true, std::memory_order_release);
    }

    m_pendingUploads.clear();
    m_swarms.clear();
    m_freeBoidSpans.clear();

    for (const auto& gc : m_garbageSets) {
        vkFreeDescriptorSets(m_device, m_descriptorPool, static_cast<uint32_t>(gc.sets.size()), gc.sets.data());
    }
    m_garbageSets.clear();

    vkDestroyPipeline(m_device, m_computePipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_computePipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_computeSetLayout, nullptr);
    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);

    vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_graphicsPipelineLayout, nullptr);
}

uint32_t BoidRenderer::AllocateSpace(uint32_t boidCount) {
    std::lock_guard<std::mutex> lock(m_allocMutex);
    uint32_t aligned = (boidCount + 7) & ~7; 

    auto it = std::find_if(m_freeBoidSpans.begin(), m_freeBoidSpans.end(),
        [&](const FreeSpan& s) { return s.count >= aligned; });
    if (it != m_freeBoidSpans.end()) {
        uint32_t offset = it->offset;
        if (it->count == aligned) m_freeBoidSpans.erase(it);
        else { it->offset += aligned; it->count -= aligned; }
        return offset;
    }
    else {
        uint32_t offset = m_nextBoidOffset.fetch_add(aligned);
        if (offset + aligned > m_maxBoids) throw std::runtime_error("Boid buffer overflow");
        return offset;
    }
}

void BoidRenderer::AddSwarms(int64_t chunkKey, const std::vector<SwarmData>& swarmsData) {
    if (swarmsData.empty()) return;

    auto pendingIt = m_pendingUploads.find(chunkKey);
    if (pendingIt != m_pendingUploads.end()) {
        pendingIt->second->store(true, std::memory_order_release);
        m_pendingUploads.erase(pendingIt);
    }

    if (m_swarms.find(chunkKey) != m_swarms.end()) {
        RemoveSwarms(chunkKey);
    }

    auto cancelToken = std::make_shared<std::atomic_bool>(false);
    m_pendingUploads[chunkKey] = cancelToken;

    std::vector<CopyRegion> allRegions;
    auto swarmsList = std::make_shared<std::vector<BoidSwarmGPU>>();

    for (const auto& swarm : swarmsData) {
        if (swarm.instances.empty()) continue;

        const uint32_t boidCount = static_cast<uint32_t>(swarm.instances.size());
        const uint32_t boidOffset = AllocateSpace(boidCount);

        BoidSwarmGPU gpuSwarm{};
        gpuSwarm.boidOffset = boidOffset;
        gpuSwarm.boidCount = boidCount;
        gpuSwarm.behavior = swarm.behavior;
        gpuSwarm.pingPongIndex = 0;

        for (const auto& b : swarm.instances) gpuSwarm.baseCenter += glm::vec3(b.position);
        gpuSwarm.baseCenter /= static_cast<float>(boidCount);
        gpuSwarm.currentCenter = gpuSwarm.baseCenter;

        const VkDeviceSize offsetBytes = static_cast<VkDeviceSize>(boidOffset) * sizeof(BoidInstance);
        const VkDeviceSize sizeBytes = static_cast<VkDeviceSize>(boidCount) * sizeof(BoidInstance);

        for (uint32_t i = 0; i < m_framesInFlight; ++i) {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = m_descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &m_computeSetLayout;

            if (vkAllocateDescriptorSets(m_device, &allocInfo, &gpuSwarm.computeSets[i]) != VK_SUCCESS)
                throw std::runtime_error("Failed to allocate boid compute descriptor set");

            uint32_t readIdx = i;
            uint32_t writeIdx = (i + 1) % m_framesInFlight;

            VkDescriptorBufferInfo bRead{};
            bRead.buffer = m_boidBuffers[readIdx];
            bRead.offset = offsetBytes;
            bRead.range = sizeBytes;

            VkDescriptorBufferInfo bWrite{};
            bWrite.buffer = m_boidBuffers[writeIdx];
            bWrite.offset = offsetBytes;
            bWrite.range = sizeBytes;

            VkWriteDescriptorSet writes[2]{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = gpuSwarm.computeSets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].pBufferInfo = &bRead;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = gpuSwarm.computeSets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].pBufferInfo = &bWrite;

            vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
        }

        swarmsList->push_back(gpuSwarm);

        CopyRegion r{};
        r.srcData = swarm.instances.data();
        r.size = sizeBytes;
        r.dstBuffer = m_boidBuffers[0];
        r.dstOffset = offsetBytes;
        allRegions.push_back(r);
    }

    if (allRegions.empty()) return;

    m_uploader->QueueBatchUpload(allRegions, [this, chunkKey, swarmsList, cancelToken]() {
        const auto activeIt = m_pendingUploads.find(chunkKey);
        const bool isCurrent = (activeIt != m_pendingUploads.end() && activeIt->second == cancelToken);
        if (isCurrent) m_pendingUploads.erase(activeIt);

        if (!isCurrent || cancelToken->load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(m_allocMutex);
            for (auto& s : *swarmsList) {
                m_freeBoidSpans.push_back({ s.boidOffset, (s.boidCount + 7u) & ~7u });
                vkFreeDescriptorSets(m_device, m_descriptorPool, m_framesInFlight, s.computeSets);
            }
            return;
        }
        m_swarms[chunkKey] = *swarmsList;
        });
}

void BoidRenderer::RemoveSwarms(int64_t chunkKey) {
    auto pendingIt = m_pendingUploads.find(chunkKey);
    if (pendingIt != m_pendingUploads.end()) {
        pendingIt->second->store(true, std::memory_order_release);
        m_pendingUploads.erase(pendingIt);
    }

    auto it = m_swarms.find(chunkKey);
    if (it == m_swarms.end()) return;

    BoidGarbage garbage{};
    garbage.safeFrame = m_renderer->GetFrameCounter() + m_renderer->GetFramesInFlight();

    std::lock_guard<std::mutex> lock(m_allocMutex);
    for (auto& s : it->second) {
        m_freeBoidSpans.push_back({ s.boidOffset, (s.boidCount + 7u) & ~7u });

        for (uint32_t i = 0; i < m_framesInFlight; ++i) {
            garbage.sets.push_back(s.computeSets[i]);
        }
    }

    if (!garbage.sets.empty()) m_garbageSets.push_back(garbage);
    m_swarms.erase(it);
}

void BoidRenderer::TickCompute(VkCommandBuffer computeCmd, float deltaTime) {
    uint64_t currentFrame = m_renderer->GetFrameCounter();
    for (auto it = m_garbageSets.begin(); it != m_garbageSets.end(); ) {
        if (currentFrame >= it->safeFrame) {
            vkFreeDescriptorSets(m_device, m_descriptorPool, static_cast<uint32_t>(it->sets.size()), it->sets.data());
            it = m_garbageSets.erase(it);
        }
        else {
            ++it;
        }
    }

    if (m_swarms.empty()) return;

    VkMemoryBarrier computeWaitBarrier{};
    computeWaitBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    computeWaitBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    computeWaitBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(computeCmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &computeWaitBarrier, 0, nullptr, 0, nullptr);

    vkCmdBindPipeline(computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);

    for (auto& [key, swarmList] : m_swarms) {
        for (auto& swarm : swarmList) {
            vkCmdBindDescriptorSets(computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipelineLayout, 0, 1, &swarm.computeSets[swarm.pingPongIndex], 0, nullptr);

            swarm.lifeTime += deltaTime;
            swarm.currentCenter.x = swarm.baseCenter.x + sin(swarm.lifeTime * swarm.behavior.driftSpeed) * swarm.behavior.driftRadius;
            swarm.currentCenter.z = swarm.baseCenter.z + cos(swarm.lifeTime * swarm.behavior.driftSpeed * 0.8f) * swarm.behavior.driftRadius;

            BoidComputeParams pc{};
            pc.deltaTime = deltaTime;
            pc.boidCount = swarm.boidCount;
            pc.separationRadius = swarm.behavior.separationRadius;
            pc.alignmentRadius = swarm.behavior.alignmentRadius;
            pc.cohesionRadius = swarm.behavior.cohesionRadius;
            pc.maxSpeed = swarm.behavior.maxSpeed;
            pc.minSpeed = swarm.behavior.minSpeed;
            pc.turnSpeed = swarm.behavior.turnSpeed;
            pc.centerAndRadius = glm::vec4(swarm.currentCenter.x, swarm.currentCenter.y, swarm.currentCenter.z, 60.0f);
            pc.wanderStrength = swarm.behavior.wanderStrength;

            vkCmdPushConstants(computeCmd, m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BoidComputeParams), &pc);
            vkCmdDispatch(computeCmd, (swarm.boidCount + 255) / 256, 1, 1);
        }
    }

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vkCmdPipelineBarrier(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    for (auto& [key, swarmList] : m_swarms) {
        for (auto& swarm : swarmList) 
            swarm.pingPongIndex = (swarm.pingPongIndex + 1) % m_framesInFlight;
    }
}

void BoidRenderer::Draw(VkCommandBuffer drawCmd, VkDescriptorSet sharedDescriptorSet, uint32_t& outDrawCalls) {
    if (m_swarms.empty()) return;

    vkCmdBindPipeline(drawCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
    vkCmdBindDescriptorSets(drawCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipelineLayout, 0, 1, &sharedDescriptorSet, 0, nullptr);

    for (const auto& [key, swarmList] : m_swarms) {
        for (const auto& swarm : swarmList) {
            VkBuffer buffers[] = { m_boidBuffers[swarm.pingPongIndex] };
            VkDeviceSize offsets[] = { swarm.boidOffset * sizeof(BoidInstance) };
            vkCmdBindVertexBuffers(drawCmd, 0, 1, buffers, offsets);

            BoidDrawPushConstants pc{};
            pc.textureId = swarm.behavior.textureId;
            pc.animationType = swarm.behavior.animationType;
            pc.color1 = swarm.behavior.color1;
            pc.color2 = swarm.behavior.color2;

            vkCmdPushConstants(drawCmd, m_graphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BoidDrawPushConstants), &pc);
            vkCmdDraw(drawCmd, 6, swarm.boidCount, 0, 0);
            outDrawCalls++;
        }
    }
}

void BoidRenderer::CreateComputePipeline() {
    VkDescriptorSetLayoutBinding readBinding{};
    readBinding.binding = 0;
    readBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    readBinding.descriptorCount = 1;
    readBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding writeBinding{};
    writeBinding.binding = 1;
    writeBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writeBinding.descriptorCount = 1;
    writeBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding bindings[] = { readBinding, writeBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_computeSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create boid compute layout");
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 2000;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create boid descriptor pool");
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.size = sizeof(BoidComputeParams);
    pushRange.offset = 0;

    VkPipelineLayoutCreateInfo pLayoutInfo{};
    pLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pLayoutInfo.setLayoutCount = 1;
    pLayoutInfo.pSetLayouts = &m_computeSetLayout;
    pLayoutInfo.pushConstantRangeCount = 1;
    pLayoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(m_device, &pLayoutInfo, nullptr, &m_computePipelineLayout);

    auto compCode = VulkanRenderer::ReadFile("shaders/boid.spv");
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

    vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_computePipeline);

    vkDestroyShaderModule(m_device, compModule, nullptr);
}

void BoidRenderer::CreateGraphicsPipeline(VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout sharedSetLayout, VkSampleCountFlagBits msaaSamples) {
    auto vertCode = VulkanRenderer::ReadFile("shaders/boid_vert.spv");
    auto fragCode = VulkanRenderer::ReadFile("shaders/boid_frag.spv");

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
    bindingDesc.stride = sizeof(BoidInstance);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    std::array<VkVertexInputAttributeDescription, 2> attribs{};
    attribs[0].location = 0;
    attribs[0].binding = 0;
    attribs[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribs[0].offset = offsetof(BoidInstance, position);

    attribs[1].location = 1;
    attribs[1].binding = 0;
    attribs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribs[1].offset = offsetof(BoidInstance, velocity);

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
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = msaaSamples;
    multisample.sampleShadingEnable = VK_FALSE;

    // FIX: Transparent particles should NOT write to the depth buffer!
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // FIX: Enable Alpha Blending!
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

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
    pushRange.size = sizeof(BoidDrawPushConstants);
    pushRange.offset = 0;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &sharedSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_graphicsPipelineLayout);

    VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
    pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipelineRenderingCreateInfo.colorAttachmentCount = 1;
    pipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
    pipelineRenderingCreateInfo.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &pipelineRenderingCreateInfo;
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
    pipelineInfo.layout = m_graphicsPipelineLayout;

    vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline);

    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
}