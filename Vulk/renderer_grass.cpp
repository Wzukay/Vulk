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
	m_framesInFlight = m_renderer->GetFramesInFlight();

	const uint32_t maxChunkInstances = 131072;
	const VkDeviceSize instanceRange = maxChunkInstances * sizeof(GrassInstance);
	const VkDeviceSize indirectRange = sizeof(VkDrawIndirectCommand);

	VkDeviceSize instanceBufferSize = (m_maxInstances + maxChunkInstances) * sizeof(GrassInstance);
	VkDeviceSize indirectBufferSize = (m_maxIndirect + 1) * sizeof(VkDrawIndirectCommand);

	m_renderer->CreateBuffer(instanceBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_instanceBuffer, m_instanceMemory);

	for (uint32_t i = 0; i < m_framesInFlight; ++i) {
		// Vertex Buffer flag is technically no longer needed, but Storage Buffer is required!
		m_renderer->CreateBuffer(instanceBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_culledBuffers[i], m_culledMemories[i]);
		m_renderer->CreateBuffer(indirectBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indirectBuffers[i], m_indirectMemories[i]);
	}

	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 3 * m_framesInFlight }
	};
	VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, m_framesInFlight, 1, poolSizes };
	if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_computeDescriptorPool) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create grass compute descriptor pool");
	}

	VkDescriptorSetLayoutBinding bindings[3]{};
	bindings[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
	// FIX: Explicitly allow the Vertex Shader to read from the Culled Output Buffer!
	bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, nullptr };
	bindings[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

	VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 3, bindings };
	if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_computeSetLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create grass compute layout");
	}

	VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GrassComputePushConstants) };
	VkPipelineLayoutCreateInfo pLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_computeSetLayout, 1, &pcRange };
	vkCreatePipelineLayout(m_device, &pLayoutInfo, nullptr, &m_computePipelineLayout);

	std::vector<VkDescriptorSetLayout> layouts(m_framesInFlight, m_computeSetLayout);
	VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_computeDescriptorPool, m_framesInFlight, layouts.data() };

	if (vkAllocateDescriptorSets(m_device, &allocInfo, m_globalComputeSets.data()) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate global grass descriptor sets");
	}

	for (uint32_t i = 0; i < m_framesInFlight; ++i) {
		VkDescriptorBufferInfo inInfo{ m_instanceBuffer, 0, instanceRange };
		VkDescriptorBufferInfo outInfo{ m_culledBuffers[i], 0, instanceRange };
		VkDescriptorBufferInfo indInfo{ m_indirectBuffers[i], 0, indirectRange };

		VkWriteDescriptorSet writes[3]{};
		writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_globalComputeSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, nullptr, &inInfo, nullptr };
		writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_globalComputeSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, nullptr, &outInfo, nullptr };
		writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_globalComputeSets[i], 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, nullptr, &indInfo, nullptr };

		vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
	}

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
	for (uint32_t i = 0; i < m_framesInFlight; ++i) {
		m_renderer->DestroyBuffer(m_culledBuffers[i], m_culledMemories[i]);
		m_renderer->DestroyBuffer(m_indirectBuffers[i], m_indirectMemories[i]);
	}

	m_grassChunks.clear();
	m_freeInstanceSpans.clear();
	m_freeIndirectSpans.clear();
	m_pendingFlags.clear();
}

std::pair<uint32_t, uint32_t> GrassRenderer::AllocateSpace(
	uint32_t instanceCount,
	uint32_t indirectCount)
{
	std::lock_guard<std::mutex> lock(m_allocMutex);

	const uint32_t alignedInstances = (instanceCount + 7u) & ~7u;

	const uint32_t alignedIndirects = (indirectCount + 15u) & ~15u;

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

	coalesceSpans(m_freeInstanceSpans);
	coalesceSpans(m_freeIndirectSpans);

	auto instanceIt = std::find_if(
		m_freeInstanceSpans.begin(),
		m_freeInstanceSpans.end(),
		[alignedInstances](const FreeSpan& span) {
			return span.count >= alignedInstances;
		});

	auto indirectIt = std::find_if(
		m_freeIndirectSpans.begin(),
		m_freeIndirectSpans.end(),
		[alignedIndirects](const FreeSpan& span) {
			return span.count >= alignedIndirects;
		});

	const uint32_t nextInstanceOffset =
		m_nextInstanceOffset.load(std::memory_order_relaxed);

	const uint32_t nextIndirectOffset =
		m_nextIndirectOffset.load(std::memory_order_relaxed);

	// Validate both allocations before modifying either allocator.
	if (instanceIt == m_freeInstanceSpans.end()) {
		if (alignedInstances > m_maxInstances ||
			nextInstanceOffset >
			m_maxInstances - alignedInstances) {
			throw std::runtime_error(
				"Grass instance buffer overflow");
		}
	}

	if (indirectIt == m_freeIndirectSpans.end()) {
		if (alignedIndirects > m_maxIndirect ||
			nextIndirectOffset >
			m_maxIndirect - alignedIndirects) {
			throw std::runtime_error(
				"Grass indirect buffer overflow");
		}
	}

	uint32_t instanceOffset = 0;

	if (instanceIt != m_freeInstanceSpans.end()) {
		instanceOffset = instanceIt->offset;

		if (instanceIt->count == alignedInstances) {
			m_freeInstanceSpans.erase(instanceIt);
		}
		else {
			instanceIt->offset += alignedInstances;
			instanceIt->count -= alignedInstances;
		}
	}
	else {
		instanceOffset = nextInstanceOffset;

		m_nextInstanceOffset.store(
			nextInstanceOffset + alignedInstances,
			std::memory_order_relaxed);
	}

	uint32_t indirectOffset = 0;

	if (indirectIt != m_freeIndirectSpans.end()) {
		indirectOffset = indirectIt->offset;

		if (indirectIt->count == alignedIndirects) {
			m_freeIndirectSpans.erase(indirectIt);
		}
		else {
			indirectIt->offset += alignedIndirects;
			indirectIt->count -= alignedIndirects;
		}
	}
	else {
		indirectOffset = nextIndirectOffset;

		m_nextIndirectOffset.store(
			nextIndirectOffset + alignedIndirects,
			std::memory_order_relaxed);
	}

	return { instanceOffset, indirectOffset };
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

	CopyRegion region{ grassInstances.data(), instBytes, m_instanceBuffer, instOff * sizeof(GrassInstance) };
	auto keyPtr = std::make_shared<int64_t>(key);
	auto cancelledFlag = std::make_shared<bool>(false);
	m_pendingFlags[key] = cancelledFlag;

	m_uploader->QueueBatchUpload(
		{ region },
		[this, keyPtr, grassChunk, cancelledFlag]() {
			auto pendingIt = m_pendingFlags.find(*keyPtr);

			// A completed older upload must not erase the token belonging to a
			// newer upload for the same chunk.
			const bool isCurrent =
				pendingIt != m_pendingFlags.end() &&
				pendingIt->second == cancelledFlag;

			if (isCurrent) {
				m_pendingFlags.erase(pendingIt);
			}

			if (!isCurrent || *cancelledFlag) {
				const uint32_t instAligned =
					(grassChunk->instanceCount + 7u) & ~7u;
				constexpr uint32_t indAligned = 16u;

				DeferSpanReturn(
					{ grassChunk->instanceOffset, instAligned },
					{ grassChunk->indirectOffset, indAligned },
					m_renderer->GetFrameCounter() + m_renderer->GetFramesInFlight());

				return;
			}

			auto oldIt = m_grassChunks.find(*keyPtr);
			if (oldIt != m_grassChunks.end()) {
				const uint32_t instAligned =
					(oldIt->second.instanceCount + 7u) & ~7u;
				constexpr uint32_t indAligned = 16u;

				DeferSpanReturn(
					{ oldIt->second.instanceOffset, instAligned },
					{ oldIt->second.indirectOffset, indAligned },
					m_renderer->GetFrameCounter() + m_renderer->GetFramesInFlight());

				m_grassChunks.erase(oldIt);
			}

			m_grassChunks[*keyPtr] = *grassChunk;
		});
}

void GrassRenderer::Cull(VkCommandBuffer commandBuffer, uint32_t currentFrameIndex) {
	if (m_grassChunks.empty() || m_computePipeline == VK_NULL_HANDLE) return;

	float fadeStart = g_Settings.GetGrassFadeStart();
	float fadeEnd = g_Settings.GetGrassFadeEnd();
	float lod0End = g_Settings.GetTerrainLod0End();

	if (fadeEnd > lod0End) {
		fadeEnd = lod0End - 5.0f;
		if (fadeStart > fadeEnd) fadeStart = fadeEnd * 0.8f;
	}

	const glm::vec3& camPos = m_renderer->GetCameraPosition();

	m_visibleChunksThisFrame.clear();

	for (const auto& [key, chunk] : m_grassChunks) {
		if (chunk.instanceCount == 0) continue;
		if (!m_renderer->IsSphereInFrustum(chunk.center, chunk.radius)) continue;

		float dist = glm::length(chunk.center - camPos);

		if (dist > fadeEnd + chunk.radius) continue;

		m_visibleChunksThisFrame.push_back(&chunk);

		vkCmdFillBuffer(commandBuffer, m_indirectBuffers[currentFrameIndex], chunk.indirectOffset * sizeof(VkDrawIndirectCommand) + 4, 4, 0);
	}

	if (m_visibleChunksThisFrame.empty()) return;

	VkMemoryBarrier fillBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &fillBarrier, 0, nullptr, 0, nullptr);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);

	// Hoist the static data that applies to all grass chunks this frame
	GrassComputePushConstants cPush{};
	cPush.cameraPos = camPos;
	cPush.fadeStart = fadeStart;
	cPush.totalInstances = 0; // Updated in loop
	cPush.vertexCount = 15;
	cPush.fadeEnd = fadeEnd;
	cPush._padding = 0.0f;

	const auto& planes = m_renderer->GetFrustumPlanes();
	for (int i = 0; i < 6; ++i) cPush.frustumPlanes[i] = glm::vec4(planes[i].normal, planes[i].distance);

	for (const auto* chunk : m_visibleChunksThisFrame) {
		// 1. Update the push constant for THIS specific chunk's instance count
		cPush.totalInstances = chunk->instanceCount;
		vkCmdPushConstants(commandBuffer, m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GrassComputePushConstants), &cPush);

		// 2. Define where in the giant storage buffers this chunk lives
		uint32_t dynamicOffsets[3] = {
			static_cast<uint32_t>(chunk->instanceOffset * sizeof(GrassInstance)),
			static_cast<uint32_t>(chunk->instanceOffset * sizeof(GrassInstance)),
			static_cast<uint32_t>(chunk->indirectOffset * sizeof(VkDrawIndirectCommand))
		};

		// 3. Bind the global set, applying the dynamic offsets
		vkCmdBindDescriptorSets(
			commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipelineLayout,
			0, 1, &m_globalComputeSets[currentFrameIndex], 3, dynamicOffsets
		);

		// 4. Dispatch the compute shader
		vkCmdDispatch(commandBuffer, (chunk->instanceCount + 255) / 256, 1, 1);
	}

	VkMemoryBarrier computeBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT };
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &computeBarrier, 0, nullptr, 0, nullptr);
}

void GrassRenderer::Draw(VkCommandBuffer commandBuffer, VkDescriptorSet sharedDescriptorSet, uint32_t currentFrameIndex, uint32_t& outDrawCalls, uint32_t& outVertexCount, uint32_t& outIndexCount) const {
	if (m_visibleChunksThisFrame.empty() || m_pipeline == VK_NULL_HANDLE) return;

	static auto startTime = std::chrono::high_resolution_clock::now();
	float time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();

	float fadeStart = g_Settings.GetGrassFadeStart();
	float fadeEnd = g_Settings.GetGrassFadeEnd();
	float lod0End = g_Settings.GetTerrainLod0End();

	if (fadeEnd > lod0End) {
		fadeEnd = lod0End - 5.0f;
		if (fadeStart > fadeEnd) fadeStart = fadeEnd * 0.8f;
	}

	const glm::vec3& camPos = m_renderer->GetCameraPosition();

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

	// Bind Set 0: The Global Camera UBO
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &sharedDescriptorSet, 0, nullptr);

	for (const auto* chunk : m_visibleChunksThisFrame) {
		float dist = glm::length(chunk->center - camPos);
		float lodFactor = std::clamp((dist - fadeStart) / (fadeEnd - fadeStart), 0.0f, 1.0f);

		GrassPushConstants push{};
		push.time = time;
		push.textureId = 1;
		push.windStrength = 1.7f;
		push.windSpeed = 2.5f;
		push.lodFactor = lodFactor;

		vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GrassPushConstants), &push);

		uint32_t dynamicOffsets[3] = {
			static_cast<uint32_t>(chunk->instanceOffset * sizeof(GrassInstance)),
			static_cast<uint32_t>(chunk->instanceOffset * sizeof(GrassInstance)),
			static_cast<uint32_t>(chunk->indirectOffset * sizeof(VkDrawIndirectCommand))
		};

		vkCmdBindDescriptorSets(
			commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
			1, 1, &m_globalComputeSets[currentFrameIndex], 3, dynamicOffsets
		);

		vkCmdDrawIndirect(commandBuffer, m_indirectBuffers[currentFrameIndex], chunk->indirectOffset * sizeof(VkDrawIndirectCommand), 1, sizeof(VkDrawIndirectCommand));
		outDrawCalls++;

		outVertexCount += chunk->instanceCount * 15;
		outIndexCount += chunk->instanceCount * 15;
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
		DeferSpanReturn({ it2->second.instanceOffset, instAligned }, { it2->second.indirectOffset, indAligned }, m_renderer->GetFrameCounter() + m_renderer->GetFramesInFlight());

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

	// FIX: Zero out all vertex input descriptions to completely bypass the Input Assembler
	VkPipelineVertexInputStateCreateInfo vertexInput{};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 0;
	vertexInput.pVertexBindingDescriptions = nullptr;
	vertexInput.vertexAttributeDescriptionCount = 0;
	vertexInput.pVertexAttributeDescriptions = nullptr;

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

	// FIX: Map both descriptor sets (0: Global UBO/Textures, 1: SSBO Culled Instances)
	VkDescriptorSetLayout layouts[] = { sharedSetLayout, m_computeSetLayout };

	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 2;
	layoutInfo.pSetLayouts = layouts;
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