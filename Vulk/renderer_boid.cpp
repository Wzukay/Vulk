#include "renderer_boid.h"
#include "renderer.h"
#include <stdexcept>

void BoidRenderer::Init(VkDevice device, VulkanRenderer* renderer, VkFormat colorFormat, VkFormat depthFormat, VkDescriptorSetLayout sharedSetLayout, VkSampleCountFlagBits msaaSamples) {
    m_device = device;
    m_renderer = renderer;

    CreateComputePipeline();
    CreateGraphicsPipeline(colorFormat, depthFormat, sharedSetLayout, msaaSamples);
}

void BoidRenderer::Cleanup() {
    if (m_device == VK_NULL_HANDLE) return;

    for (auto& [key, swarm] : m_swarms) {
        for (int i = 0; i < 2; i++) {
            m_renderer->DestroyBuffer(swarm.buffers[i], swarm.memories[i]);
        }
    }
    m_swarms.clear();

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

void BoidRenderer::AddSwarm(int64_t chunkKey, const std::vector<BoidInstance>& initialBoids, uint32_t textureId) {
    // Prevent memory leaks when terrain chunk LODs update and regenerate swarms
    if (m_swarms.find(chunkKey) != m_swarms.end()) {
        RemoveSwarm(chunkKey);
    }

    BoidSwarmGPU swarm;
    swarm.boidCount = static_cast<uint32_t>(initialBoids.size());
    swarm.textureId = textureId;

    if (!initialBoids.empty()) {
        swarm.baseCenter = glm::vec3(initialBoids[0].position);
        swarm.currentCenter = swarm.baseCenter;
        swarm.lifeTime = 0.0f; // Start clock
    }

    VkDeviceSize bufferSize = sizeof(BoidInstance) * swarm.boidCount;

    for (int i = 0; i < 2; i++) {
        m_renderer->CreateBuffer(bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            swarm.buffers[i], swarm.memories[i]);
    }

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    m_renderer->CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory);

    void* data;
    vkMapMemory(m_device, stagingMemory, 0, bufferSize, 0, &data);
    memcpy(data, initialBoids.data(), bufferSize);
    vkUnmapMemory(m_device, stagingMemory);

    VkCommandBuffer cmd = m_renderer->BeginSingleTimeCommands();
    VkBufferCopy copyRegion{ 0, 0, bufferSize };
    vkCmdCopyBuffer(cmd, stagingBuffer, swarm.buffers[0], 1, &copyRegion);
    m_renderer->EndSingleTimeCommands(cmd);
    m_renderer->DestroyBuffer(stagingBuffer, stagingMemory);

    VkDescriptorSetLayout layouts[] = { m_computeSetLayout, m_computeSetLayout };
    VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_descriptorPool, 2, layouts };
    vkAllocateDescriptorSets(m_device, &allocInfo, swarm.computeSets);

    VkDescriptorBufferInfo bInfo0{ swarm.buffers[0], 0, bufferSize };
    VkDescriptorBufferInfo bInfo1{ swarm.buffers[1], 0, bufferSize };

    VkWriteDescriptorSet writes[4]{};
    writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, swarm.computeSets[0], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bInfo0, nullptr };
    writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, swarm.computeSets[0], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bInfo1, nullptr };
    writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, swarm.computeSets[1], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bInfo1, nullptr };
    writes[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, swarm.computeSets[1], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bInfo0, nullptr };
    vkUpdateDescriptorSets(m_device, 4, writes, 0, nullptr);

    m_swarms[chunkKey] = swarm;
}

void BoidRenderer::RemoveSwarm(int64_t chunkKey) {
    auto it = m_swarms.find(chunkKey);
    if (it != m_swarms.end()) {
        BufferDeletion del;
        del.buffers.push_back(it->second.buffers[0]);
        del.buffers.push_back(it->second.buffers[1]);
        del.memories.push_back(it->second.memories[0]);
        del.memories.push_back(it->second.memories[1]);
        m_renderer->DeferBufferDeletion(std::move(del));

        // Defer freeing descriptor sets to avoid in-flight command buffer errors
        BoidGarbage gc;
        gc.sets[0] = it->second.computeSets[0];
        gc.sets[1] = it->second.computeSets[1];
        gc.safeFrame = m_renderer->GetFrameCounter() + 3; // MAX_FRAMES_IN_FLIGHT
        m_garbageSets.push_back(gc);

        m_swarms.erase(it);
    }
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

    for (auto& [key, swarm] : m_swarms) {
        vkCmdBindDescriptorSets(computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipelineLayout, 0, 1, &swarm.computeSets[swarm.pingPongIndex], 0, nullptr);

        // Advance time for this specific swarm
        swarm.lifeTime += deltaTime;

        // Slowly drift the swarm's target destination across the chunk
        float driftSpeed = 0.8f; // Increased from 0.3f
        float driftRadius = 150.0f; // Let them explore a massive 300-unit area of the chunk
        swarm.currentCenter.x = swarm.baseCenter.x + sin(swarm.lifeTime * driftSpeed) * driftRadius;
        swarm.currentCenter.z = swarm.baseCenter.z + cos(swarm.lifeTime * driftSpeed * 0.8f) * driftRadius;

        BoidComputeParams pc{};
        pc.deltaTime = deltaTime;
        pc.boidCount = swarm.boidCount;

        pc.separationRadius = 12.0f;
        pc.alignmentRadius = 0.0f;
        pc.cohesionRadius = 20.0f;

        // Faster cruising speeds
        pc.maxSpeed = 15.0f;
        pc.minSpeed = 6.0f;
        pc.turnSpeed = 5.0f;

        pc.centerAndRadius = glm::vec4(swarm.currentCenter, 60.0f);
        pc.wanderStrength = 2.5f;

        vkCmdPushConstants(computeCmd, m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BoidComputeParams), &pc);

        uint32_t groupCount = (swarm.boidCount + 255) / 256;
        vkCmdDispatch(computeCmd, groupCount, 1, 1);
    }

    // Single global barrier for ALL swarms at once (huge performance boost over barrier-per-chunk)
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vkCmdPipelineBarrier(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    // Swap indices for next frame
    for (auto& [key, swarm] : m_swarms) {
        swarm.pingPongIndex = 1 - swarm.pingPongIndex;
    }
}

void BoidRenderer::Draw(VkCommandBuffer drawCmd, VkDescriptorSet sharedDescriptorSet, uint32_t& outDrawCalls) {
    if (m_swarms.empty()) return;

    vkCmdBindPipeline(drawCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
    vkCmdBindDescriptorSets(drawCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipelineLayout, 0, 1, &sharedDescriptorSet, 0, nullptr);

    for (const auto& [key, swarm] : m_swarms) {
        VkBuffer buffers[] = { swarm.buffers[swarm.pingPongIndex] };
        VkDeviceSize offsets[] = { 0 };
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
    vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_computeSetLayout);

    // Increased pool capacity to handle hundreds of active chunks simultaneously
    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2000 };
    // Added FREE_DESCRIPTOR_SET flag so RemoveSwarm doesn't cause pool fragmentation
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, 1000, 1, &poolSize };
    vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool);

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