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

    VkDeviceSize bufferSize = m_maxBoids * sizeof(BoidInstance);
    for (int i = 0; i < 2; i++) {
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

    for (int i = 0; i < 2; i++) {
        m_renderer->DestroyBuffer(m_boidBuffers[i], m_boidMemories[i]);
    }

    for (auto& [key, token] : m_pendingUploads) {
        token->store(true, std::memory_order_release);
    }

    m_pendingUploads.clear();
    m_swarms.clear();
    m_freeBoidSpans.clear();

    for (const auto& gc : m_garbageSets) {
        vkFreeDescriptorSets(m_device, m_descriptorPool, 2, gc.sets);
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
    uint32_t aligned = (boidCount + 7) & ~7; // Pad to multiple of 8 (256 bytes)

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

void BoidRenderer::AddSwarm(
    int64_t chunkKey,
    const std::vector<BoidInstance>& initialBoids,
    uint32_t textureId)
{
    if (initialBoids.empty()) {
        return;
    }

    // Cancel an upload for an older version of this chunk.
    auto pendingIt = m_pendingUploads.find(chunkKey);
    if (pendingIt != m_pendingUploads.end()) {
        pendingIt->second->store(true, std::memory_order_release);
        m_pendingUploads.erase(pendingIt);
    }

    // Remove an already-visible swarm before replacing it.
    if (m_swarms.find(chunkKey) != m_swarms.end()) {
        RemoveSwarm(chunkKey);
    }

    const uint32_t boidCount =
        static_cast<uint32_t>(initialBoids.size());
    const uint32_t boidOffset = AllocateSpace(boidCount);

    auto swarm = std::make_shared<BoidSwarmGPU>();
    swarm->boidOffset = boidOffset;
    swarm->boidCount = boidCount;
    swarm->textureId = textureId;
    swarm->pingPongIndex = 0;

    for (const BoidInstance& boid : initialBoids) {
        swarm->baseCenter += glm::vec3(boid.position);
    }

    swarm->baseCenter /= static_cast<float>(boidCount);
    swarm->currentCenter = swarm->baseCenter;

    const VkDeviceSize offsetBytes =
        static_cast<VkDeviceSize>(boidOffset) * sizeof(BoidInstance);
    const VkDeviceSize sizeBytes =
        static_cast<VkDeviceSize>(boidCount) * sizeof(BoidInstance);

    for (int i = 0; i < 2; ++i) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_computeSetLayout;

        if (vkAllocateDescriptorSets(
            m_device,
            &allocInfo,
            &swarm->computeSets[i]) != VK_SUCCESS) {
            throw std::runtime_error(
                "Failed to allocate boid compute descriptor set");
        }
    }

    VkDescriptorBufferInfo bufferInfo0{};
    bufferInfo0.buffer = m_boidBuffers[0];
    bufferInfo0.offset = offsetBytes;
    bufferInfo0.range = sizeBytes;

    VkDescriptorBufferInfo bufferInfo1{};
    bufferInfo1.buffer = m_boidBuffers[1];
    bufferInfo1.offset = offsetBytes;
    bufferInfo1.range = sizeBytes;

    VkWriteDescriptorSet writes[4]{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = swarm->computeSets[0];
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &bufferInfo0;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = swarm->computeSets[0];
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &bufferInfo1;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = swarm->computeSets[1];
    writes[2].dstBinding = 0;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].pBufferInfo = &bufferInfo1;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = swarm->computeSets[1];
    writes[3].dstBinding = 1;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].pBufferInfo = &bufferInfo0;

    vkUpdateDescriptorSets(m_device, 4, writes, 0, nullptr);

    auto cancelToken = std::make_shared<std::atomic_bool>(false);
    m_pendingUploads[chunkKey] = cancelToken;

    CopyRegion uploadRegion{};
    uploadRegion.srcData = initialBoids.data();
    uploadRegion.size = sizeBytes;
    uploadRegion.dstBuffer = m_boidBuffers[0];
    uploadRegion.dstOffset = offsetBytes;

    m_uploader->QueueBatchUpload(
        { uploadRegion },
        [this, chunkKey, swarm, cancelToken]() {
            const auto activeIt = m_pendingUploads.find(chunkKey);

            // Do not let an old callback erase a newer upload's token.
            const bool isCurrent =
                activeIt != m_pendingUploads.end() &&
                activeIt->second == cancelToken;

            if (isCurrent) {
                m_pendingUploads.erase(activeIt);
            }

            if (!isCurrent ||
                cancelToken->load(std::memory_order_acquire)) {
                const uint32_t aligned =
                    (swarm->boidCount + 7u) & ~7u;

                {
                    std::lock_guard<std::mutex> lock(m_allocMutex);
                    m_freeBoidSpans.push_back({
                        swarm->boidOffset,
                        aligned
                        });
                }

                vkFreeDescriptorSets(
                    m_device,
                    m_descriptorPool,
                    2,
                    swarm->computeSets);

                return;
            }

            m_swarms[chunkKey] = *swarm;
        });
}

void BoidRenderer::RemoveSwarm(int64_t chunkKey) {
    auto pendingIt = m_pendingUploads.find(chunkKey);
    if (pendingIt != m_pendingUploads.end()) {
        pendingIt->second->store(true, std::memory_order_release);
        m_pendingUploads.erase(pendingIt);
    }

    auto it = m_swarms.find(chunkKey);
    if (it == m_swarms.end()) {
        return;
    }

    const uint32_t aligned =
        (it->second.boidCount + 7u) & ~7u;

    {
        std::lock_guard<std::mutex> lock(m_allocMutex);
        m_freeBoidSpans.push_back({
            it->second.boidOffset,
            aligned
            });
    }

    BoidGarbage garbage{};
    garbage.sets[0] = it->second.computeSets[0];
    garbage.sets[1] = it->second.computeSets[1];
    garbage.safeFrame = m_renderer->GetFrameCounter() + m_renderer->GetFramesInFlight();

    m_garbageSets.push_back(garbage);
    m_swarms.erase(it);
}

void BoidRenderer::TickCompute(VkCommandBuffer computeCmd, float deltaTime) {
    uint64_t currentFrame = m_renderer->GetFrameCounter();
    for (auto it = m_garbageSets.begin(); it != m_garbageSets.end(); ) {
        if (currentFrame >= it->safeFrame) {
            vkFreeDescriptorSets(m_device, m_descriptorPool, 2, it->sets);
            it = m_garbageSets.erase(it);
        }
        else {
            ++it;
        }
    }

    if (m_swarms.empty()) return;

    vkCmdBindPipeline(computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);

    // Apply Fix 5: Hoist static parameters outside the loop
    BoidComputeParams basePc{};
    basePc.deltaTime = deltaTime;
    basePc.separationRadius = 12.0f;
    basePc.alignmentRadius = 0.0f;
    basePc.cohesionRadius = 20.0f;
    basePc.maxSpeed = 15.0f;
    basePc.minSpeed = 6.0f;
    basePc.turnSpeed = 5.0f;
    basePc.wanderStrength = 2.5f;

    for (auto& [key, swarm] : m_swarms) {
        vkCmdBindDescriptorSets(computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipelineLayout, 0, 1, &swarm.computeSets[swarm.pingPongIndex], 0, nullptr);

        swarm.lifeTime += deltaTime;
        float driftSpeed = 0.8f;
        float driftRadius = 150.0f;
        swarm.currentCenter.x = swarm.baseCenter.x + sin(swarm.lifeTime * driftSpeed) * driftRadius;
        swarm.currentCenter.z = swarm.baseCenter.z + cos(swarm.lifeTime * driftSpeed * 0.8f) * driftRadius;

        basePc.boidCount = swarm.boidCount;
        basePc.centerAndRadius = glm::vec4(swarm.currentCenter, 60.0f);

        vkCmdPushConstants(computeCmd, m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BoidComputeParams), &basePc);

        uint32_t groupCount = (swarm.boidCount + 255) / 256;
        vkCmdDispatch(computeCmd, groupCount, 1, 1);
    }

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vkCmdPipelineBarrier(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    for (auto& [key, swarm] : m_swarms) {
        swarm.pingPongIndex = 1 - swarm.pingPongIndex;
    }
}

void BoidRenderer::Draw(VkCommandBuffer drawCmd, VkDescriptorSet sharedDescriptorSet, uint32_t& outDrawCalls) {
    if (m_swarms.empty()) return;

    vkCmdBindPipeline(drawCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
    vkCmdBindDescriptorSets(drawCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipelineLayout, 0, 1, &sharedDescriptorSet, 0, nullptr);

    for (const auto& [key, swarm] : m_swarms) {
        VkBuffer buffers[] = { m_boidBuffers[swarm.pingPongIndex] };
        VkDeviceSize offsets[] = { swarm.boidOffset * sizeof(BoidInstance) };
        vkCmdBindVertexBuffers(drawCmd, 0, 1, buffers, offsets);

        vkCmdPushConstants(drawCmd, m_graphicsPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t), &swarm.textureId);
        vkCmdDraw(drawCmd, 6, swarm.boidCount, 0, 0);
        outDrawCalls++;
    }
}

void BoidRenderer::CreateComputePipeline() {
    VkDescriptorSetLayoutBinding readBinding{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutBinding writeBinding{ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutBinding bindings[] = { readBinding, writeBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 2, bindings };
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_computeSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create boid compute layout");
    }

    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2000 };
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, 1000, 1, &poolSize };
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create boid descriptor pool");
    }

    VkPushConstantRange pushRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BoidComputeParams) };
    VkPipelineLayoutCreateInfo pLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_computeSetLayout, 1, &pushRange };
    vkCreatePipelineLayout(m_device, &pLayoutInfo, nullptr, &m_computePipelineLayout);

    auto compCode = VulkanRenderer::ReadFile("shaders/boid.spv");
    VkShaderModule compModule = VulkanRenderer::CreateShaderModule(m_device, compCode);

    VkPipelineShaderStageCreateInfo stageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, compModule, "main" };
    VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, stageInfo, m_computePipelineLayout, VK_NULL_HANDLE, 0 };
    vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_computePipeline);

    vkDestroyShaderModule(m_device, compModule, nullptr);
}

void BoidRenderer::CreateGraphicsPipeline(VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout sharedSetLayout, VkSampleCountFlagBits msaaSamples) {
    auto vertCode = VulkanRenderer::ReadFile("shaders/boid_vert.spv");
    auto fragCode = VulkanRenderer::ReadFile("shaders/boid_frag.spv");

    VkShaderModule vertModule = VulkanRenderer::CreateShaderModule(m_device, vertCode);
    VkShaderModule fragModule = VulkanRenderer::CreateShaderModule(m_device, fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" };
    stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" };

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(BoidInstance);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    std::array<VkVertexInputAttributeDescription, 2> attribs{};
    attribs[0] = { 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(BoidInstance, position) };
    attribs[1] = { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(BoidInstance, velocity) };

    VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
    vertexInput.pVertexAttributeDescriptions = attribs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
    VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };

    VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f };
    VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, msaaSamples, VK_FALSE, 1.0f, nullptr, VK_FALSE, VK_FALSE };
    VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS, VK_FALSE, VK_FALSE };

    VkPipelineColorBlendAttachmentState blendAttachment{ VK_FALSE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, 0xF };
    VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &blendAttachment };

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dynamicStates };

    VkPushConstantRange pushRange{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t) };
    VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &sharedSetLayout, 1, &pushRange };
    vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_graphicsPipelineLayout);

    VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, nullptr, 0, 1, &colorFormat, depthFormat };

    VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &pipelineRenderingCreateInfo, 0, 2, stages, &vertexInput, &inputAssembly, nullptr, &viewportState, &rasterizer, &multisample, &depthStencil, &colorBlending, &dynamicState, m_graphicsPipelineLayout, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, -1 };
    vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline);

    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
}