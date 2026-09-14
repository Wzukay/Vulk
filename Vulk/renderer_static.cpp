#include "renderer_static.h"
#include "renderer.h"
#include "asset_manager.h"
#include "settings.h"

#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <glm/gtx/norm.hpp>

void StaticMeshRenderer::Init(
	VkDevice device,
	VulkanRenderer* renderer)
{
	m_device = device;
	m_renderer = renderer;

	m_framesInFlight = std::min(MAX_FRAMES_IN_FLIGHT_COUNT, m_renderer->GetFramesInFlight());

	m_maxVertices = MAX_GLOBAL_VERTICES;
	m_maxIndices = MAX_GLOBAL_INDICES;

	const VkDeviceSize vertexSize = sizeof(ModelVertex) * m_maxVertices;
	const VkDeviceSize indexSize = sizeof(uint16_t) * m_maxIndices;

	m_renderer->CreateBuffer(
		vertexSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
		VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		m_vertexBuffer,
		m_vertexMemory);

	m_renderer->CreateBuffer(
		indexSize,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
		VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		m_indexBuffer,
		m_indexMemory);

	const VkDeviceSize visibleInstanceBytes =
		sizeof(InstanceData) * m_maxInstances;

	m_cullInputStride =
		sizeof(StaticInstanceCullData) * m_maxInstances;

	m_renderer->CreateBuffer(
		m_cullInputStride * m_framesInFlight,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		m_cullInputBuffer,
		m_cullInputMemory);

	vkMapMemory(
		m_device,
		m_cullInputMemory,
		0,
		VK_WHOLE_SIZE,
		0,
		reinterpret_cast<void**>(&m_mappedCullInput));

	for (uint32_t i = 0; i < m_framesInFlight; ++i) {
		m_renderer->CreateBuffer(
			visibleInstanceBytes,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			m_visibleInstanceBuffers[i],
			m_visibleInstanceMemories[i]);
		
		m_renderer->CreateBuffer(
			sizeof(VkDrawIndexedIndirectCommand) * MAX_INDIRECT_BATCHES,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			m_indirectCommandBuffers[i],
			m_indirectCommandMemories[i]);

		m_renderer->CreateBuffer(
			sizeof(VkDrawIndexedIndirectCommand) * MAX_INDIRECT_BATCHES,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			m_indirectReadbackBuffers[i],
			m_indirectReadbackMemories[i]);
	
		vkMapMemory(m_device, m_indirectReadbackMemories[i], 0, VK_WHOLE_SIZE, 0, &m_mappedIndirectReadback[i]);
	}

	CreateCullPipeline();
	UpdateCullDescriptors();
}

void StaticMeshRenderer::Cleanup() {
	if (m_mappedCullInput != nullptr) {
		vkUnmapMemory(m_device, m_cullInputMemory);
		m_mappedCullInput = nullptr;
	}

	for (size_t i = 0; i < m_framesInFlight; ++i) {
		if (m_queryPools[i] != VK_NULL_HANDLE) {
			vkDestroyQueryPool(m_device, m_queryPools[i], nullptr);
			m_queryPools[i] = VK_NULL_HANDLE;
		}
	}

	m_renderer->DestroyBuffer(m_vertexBuffer, m_vertexMemory);
	m_renderer->DestroyBuffer(m_indexBuffer, m_indexMemory);
	if (m_cullPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_cullPipeline, nullptr);
	if (m_cullPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_cullPipelineLayout, nullptr);
	if (m_cullDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_device, m_cullDescriptorPool, nullptr);
	if (m_cullDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_cullDescriptorSetLayout, nullptr);
	for (uint32_t i = 0; i < m_framesInFlight; ++i) {
		m_renderer->DestroyBuffer(m_visibleInstanceBuffers[i], m_visibleInstanceMemories[i]);
		m_renderer->DestroyBuffer(m_indirectCommandBuffers[i], m_indirectCommandMemories[i]);

		vkUnmapMemory(m_device, m_indirectReadbackMemories[i]);
		m_renderer->DestroyBuffer(m_indirectReadbackBuffers[i], m_indirectReadbackMemories[i]);
	}
	m_renderer->DestroyBuffer(m_cullInputBuffer, m_cullInputMemory);
}

void StaticMeshRenderer::ResizeBuffers(uint32_t requiredVertices, uint32_t requiredIndices) {
	vkDeviceWaitIdle(m_device);
	m_renderer->DestroyBuffer(m_vertexBuffer, m_vertexMemory);
	m_renderer->DestroyBuffer(m_indexBuffer, m_indexMemory);

	m_maxVertices = requiredVertices;
	m_maxIndices = requiredIndices;

	m_renderer->CreateBuffer(sizeof(ModelVertex) * m_maxVertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vertexBuffer, m_vertexMemory);
	m_renderer->CreateBuffer(sizeof(uint16_t) * m_maxIndices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indexBuffer, m_indexMemory);
}

void StaticMeshRenderer::UploadUniqueMeshes(const std::unordered_set<uint32_t>& uniqueMeshHashes) {
	std::vector<ModelVertex> allVerts;
	std::vector<uint16_t> allIndices;
	m_meshAllocations.clear();

	uint32_t vertexOffset = 0;
	uint32_t indexOffset = 0;

	for (const auto& hash : uniqueMeshHashes) {
		std::string name = StringHash::Get(hash);
		const LodGroup* group = g_AssetManager.GetLodGroup(name);
		MeshBufferAllocation alloc;
		alloc.firstIndex = indexOffset;
		alloc.vertexOffset = vertexOffset;
		alloc.maxBoundingRadius = 0.0f;

		if (group) {
			for (const auto& path : group->meshPaths) {
				MeshAsset* mesh = g_AssetManager.GetMesh(path);
				if (!mesh || mesh->subMeshes.empty()) continue;

				// Force 1 submesh per LOD to keep indirect batching contiguous
				SubMesh sub = mesh->subMeshes[0];
				sub.textureId = g_AssetManager.GetTextureId(mesh->materialTextures[0]);
				sub.normalTextureId = g_AssetManager.GetNormalTextureId(mesh->normalMapTextures[0]);

				alloc.maxBoundingRadius = std::max(alloc.maxBoundingRadius, sub.boundingRadiusLocal);
				alloc.subMeshes.push_back(sub);

				allVerts.insert(allVerts.end(), mesh->vertices.begin(), mesh->vertices.end());
				for (uint32_t idx : mesh->indices) {
					if (idx > 65535) throw std::runtime_error("Mesh exceeds 16-bit index limit!");
					allIndices.push_back(static_cast<uint16_t>(idx));
				}
				allIndices.insert(allIndices.end(), mesh->indices.begin(), mesh->indices.end());

				vertexOffset += static_cast<uint32_t>(mesh->vertices.size());
				indexOffset += static_cast<uint32_t>(mesh->indices.size());
			}

			alloc.lodCount = std::max(1u, static_cast<uint32_t>(alloc.subMeshes.size()));
		}
		else {
			MeshAsset* mesh = g_AssetManager.GetMesh(name);
			if (!mesh) continue;

			alloc.lodCount = 1;
			for (size_t subIdx = 0; subIdx < mesh->subMeshes.size(); ++subIdx) {
				SubMesh sub = mesh->subMeshes[subIdx];
				sub.textureId = g_AssetManager.GetTextureId(mesh->materialTextures[subIdx]);
				sub.normalTextureId = g_AssetManager.GetNormalTextureId(mesh->normalMapTextures[subIdx]);

				alloc.maxBoundingRadius = std::max(alloc.maxBoundingRadius, sub.boundingRadiusLocal);
				alloc.subMeshes.push_back(sub);
			}

			allVerts.insert(allVerts.end(), mesh->vertices.begin(), mesh->vertices.end());
			for (uint32_t idx : mesh->indices) {
				if (idx > 65535) throw std::runtime_error("Mesh exceeds 16-bit index limit!");
				allIndices.push_back(static_cast<uint16_t>(idx));
			}
			allIndices.insert(allIndices.end(), mesh->indices.begin(), mesh->indices.end());

			vertexOffset += static_cast<uint32_t>(mesh->vertices.size());
			indexOffset += static_cast<uint32_t>(mesh->indices.size());
		}

		m_meshAllocations[hash] = alloc;
	}

	if (allVerts.empty() || allIndices.empty()) return;

	if (allVerts.size() > m_maxVertices || allIndices.size() > m_maxIndices) {
		uint32_t targetVerts = std::max<uint32_t>(static_cast<uint32_t>(allVerts.size() * 2), static_cast<uint32_t>(m_maxVertices));
		uint32_t targetIndices = std::max<uint32_t>(static_cast<uint32_t>(allIndices.size() * 2), static_cast<uint32_t>(m_maxIndices));
		ResizeBuffers(targetVerts, targetIndices);
	}

	VkBuffer stagingVert, stagingIndex;
	VkDeviceMemory stagingVertMem, stagingIndexMem;
	m_renderer->CreateBuffer(sizeof(ModelVertex) * allVerts.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingVert, stagingVertMem);
	m_renderer->CreateBuffer(sizeof(uint16_t) * allIndices.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingIndex, stagingIndexMem);

	void* data;
	vkMapMemory(m_device, stagingVertMem, 0, sizeof(ModelVertex) * allVerts.size(), 0, &data);
	memcpy(data, allVerts.data(), sizeof(ModelVertex) * allVerts.size());
	vkUnmapMemory(m_device, stagingVertMem);

	vkMapMemory(m_device, stagingIndexMem, 0, sizeof(uint16_t) * allIndices.size(), 0, &data);
	memcpy(data, allIndices.data(), sizeof(uint16_t) * allIndices.size());
	vkUnmapMemory(m_device, stagingIndexMem);

	VkCommandBuffer cmd = m_renderer->BeginSingleTimeCommands();
	VkBufferCopy vCopy{ 0, 0, sizeof(ModelVertex) * allVerts.size() };
	vkCmdCopyBuffer(cmd, stagingVert, m_vertexBuffer, 1, &vCopy);
	VkBufferCopy iCopy{ 0, 0, sizeof(uint16_t) * allIndices.size() };
	vkCmdCopyBuffer(cmd, stagingIndex, m_indexBuffer, 1, &iCopy);
	m_renderer->EndSingleTimeCommands(cmd);

	m_renderer->DestroyBuffer(stagingVert, stagingVertMem);
	m_renderer->DestroyBuffer(stagingIndex, stagingIndexMem);
}

void StaticMeshRenderer::UpdateScene(const Scene& scene) {
	const auto& instances = scene.GetStaticInstances();

	std::unordered_set<uint32_t> uniqueMeshHashes;
	for (const auto& inst : instances) {
		uniqueMeshHashes.insert(inst.meshHash);
	}

	bool geometryChanged = uniqueMeshHashes.size() != m_meshAllocations.size();
	if (!geometryChanged) {
		for (uint32_t hash : uniqueMeshHashes) {
			if (m_meshAllocations.find(hash) == m_meshAllocations.end()) {
				geometryChanged = true;
				break;
			}
		}
	}

	if (geometryChanged) UploadUniqueMeshes(uniqueMeshHashes);

	m_staticDrawList.clear();
	m_sceneObjects.clear();
	m_subMeshes.clear();
	m_cullInstances.clear();
	m_indirectBatches.clear();
	m_batchIndexByMeshHash.clear();

	for (const auto& inst : instances) {
		if (inst.isInstanced) {
			const auto allocIt = m_meshAllocations.find(inst.meshHash);
			if (allocIt == m_meshAllocations.end() || allocIt->second.subMeshes.empty()) continue;

			const MeshBufferAllocation& alloc = allocIt->second;

			uint32_t baseCommandIndex = 0;
			auto cacheIt = m_batchIndexByMeshHash.find(inst.meshHash);

			if (cacheIt == m_batchIndexByMeshHash.end()) {
				baseCommandIndex = static_cast<uint32_t>(m_indirectBatches.size());

				// Automatically generate sequential indirect commands for every LOD level!
				for (size_t i = 0; i < alloc.lodCount; ++i) {
					if (m_indirectBatches.size() >= MAX_INDIRECT_BATCHES) throw std::runtime_error("Static indirect batch buffer overflow");

					const SubMesh& sub = alloc.subMeshes[i];
					StaticIndirectBatch batch{};
					batch.command.indexCount = sub.indexCount;
					batch.command.instanceCount = 0;
					batch.command.firstIndex = sub.firstIndex + alloc.firstIndex;
					batch.command.vertexOffset = sub.vertexOffset + alloc.vertexOffset;
					batch.command.firstInstance = 0;
					batch.textureId = sub.textureId;
					batch.normalTextureId = sub.normalTextureId;

					m_indirectBatches.push_back(batch);
				}

				m_batchIndexByMeshHash[inst.meshHash] = baseCommandIndex;
			}
			else {
				baseCommandIndex = cacheIt->second;
			}

			if (m_cullInstances.size() >= m_maxInstances) throw std::runtime_error("Static instance buffer overflow");

			const float maxScale = std::max({
				glm::length(glm::vec3(inst.transform[0])),
				glm::length(glm::vec3(inst.transform[1])),
				glm::length(glm::vec3(inst.transform[2]))
				});

			glm::vec3 worldCenter = glm::vec3(inst.transform * glm::vec4(alloc.subMeshes[0].boundingCenterLocal, 1.0f));

			StaticInstanceCullData candidate{};
			candidate.modelMatrix = inst.transform;
			candidate.worldPositionRadius = glm::vec4(worldCenter, alloc.maxBoundingRadius * maxScale);

			// PACK THE LOD MATH
			candidate.drawData.x = baseCommandIndex;       // Base Command 
			candidate.drawData.z = alloc.lodCount - 1;     // Max allowed LOD

			m_cullInstances.push_back(candidate);

			// Reserve space in the visible buffer for every LOD possibility
			for (size_t i = 0; i < alloc.lodCount; ++i) {
				m_indirectBatches[baseCommandIndex + i].sourceCount++;
			}
			continue;
		}

		const auto allocIt = m_meshAllocations.find(inst.meshHash);

		if (allocIt == m_meshAllocations.end()) {
			continue;
		}

		const MeshBufferAllocation& alloc = allocIt->second;

		SceneObject obj{};
		obj.modelMatrix = inst.transform;
		obj.objectId = inst.objectId;

		const uint32_t objectIndex =
			static_cast<uint32_t>(m_sceneObjects.size());

		m_sceneObjects.push_back(obj);

		for (const SubMesh& sub : alloc.subMeshes) {
			SubMesh activeSub = sub;
			activeSub.vertexOffset += alloc.vertexOffset;
			activeSub.firstIndex += alloc.firstIndex;

			const uint32_t subMeshIndex =
				static_cast<uint32_t>(m_subMeshes.size());

			m_subMeshes.push_back(activeSub);

			DrawEntry entry{};
			entry.objectIndex = objectIndex;
			entry.subMeshIndex = subMeshIndex;

			entry.cachedMaxScale = std::max({
				glm::length(glm::vec3(inst.transform[0])),
				glm::length(glm::vec3(inst.transform[1])),
				glm::length(glm::vec3(inst.transform[2]))
				});

			m_staticDrawList.push_back(entry);
		}
	}

	uint32_t outputBase = 0;
	for (StaticIndirectBatch& batch : m_indirectBatches) {
		if (batch.sourceCount > m_maxInstances - outputBase) throw std::runtime_error("Static visible instance buffer overflow");
		batch.outputBase = outputBase;
		batch.command.firstInstance = outputBase;
		outputBase += batch.sourceCount;
	}

	for (StaticInstanceCullData& candidate : m_cullInstances) {
		candidate.drawData.y = m_indirectBatches[candidate.drawData.x].outputBase;
		candidate.drawData.w = m_indirectBatches[candidate.drawData.x].sourceCount;
	}
}

void StaticMeshRenderer::Draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
	VkDescriptorSet descriptorSet, const glm::vec3& cameraPos,
	const std::array<FrustumPlane, 6>& frustumPlanes, uint32_t currentFrameIndex,
	VkPipeline staticPipeline, VkPipeline instancedPipeline,
	uint32_t& outDrawCalls, uint32_t& outCulledCount,
	uint32_t& outVertexCount, uint32_t& outIndexCount) {

	// 1. NON-INSTANCED STATIC MESHES
	if (!m_staticDrawList.empty() && staticPipeline != VK_NULL_HANDLE) {
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, staticPipeline);

		VkBuffer vertexBuffers[] = { m_vertexBuffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);

		for (const auto& entry : m_staticDrawList) {
			const auto& obj = m_sceneObjects[entry.objectIndex];
			const auto& sub = m_subMeshes[entry.subMeshIndex];

			glm::vec3 worldCenter = glm::vec3(obj.modelMatrix * glm::vec4(sub.boundingCenterLocal, 1.0f));
			float worldRadius = sub.boundingRadiusLocal * entry.cachedMaxScale;

			if (!m_renderer->IsWorldSphereInFrustum(worldCenter, worldRadius)) {
				outCulledCount++;
				continue;
			}

			PushConstants constants{};
			constants.modelMatrix = obj.modelMatrix;
			constants.textureId = sub.textureId;
			constants.normalTextureId = sub.normalTextureId;

			vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &constants);
			vkCmdDrawIndexed(commandBuffer, sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);

			outDrawCalls++;
			outVertexCount += sub.indexCount;
			outIndexCount += sub.indexCount;
		}
	}

	// 2. GPU-CULLED INSTANCED MESHES (Trees, Rocks, Foliage)
	if (!m_indirectBatches.empty() && instancedPipeline != VK_NULL_HANDLE) {
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, instancedPipeline);

		VkBuffer vertexBuffers[] = { m_vertexBuffer, m_visibleInstanceBuffers[currentFrameIndex] };
		VkDeviceSize offsets[] = { 0, 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 2, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);

		for (uint32_t batchIndex = 0; batchIndex < m_indirectBatches.size(); ++batchIndex) {
			const auto& batch = m_indirectBatches[batchIndex];
			PushConstants constants{};
			constants.textureId = batch.textureId;
			constants.normalTextureId = batch.normalTextureId;
			vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &constants);
			vkCmdDrawIndexedIndirect(commandBuffer, m_indirectCommandBuffers[currentFrameIndex],
				batchIndex * sizeof(VkDrawIndexedIndirectCommand), 1, sizeof(VkDrawIndexedIndirectCommand));
			outDrawCalls++;
		}
	}
}

void StaticMeshRenderer::DrawDynamic(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
	const std::vector<MeshInstance>& dynamicInstances, VkPipeline pipeline) {
	if (dynamicInstances.empty() || pipeline == VK_NULL_HANDLE) return;

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	VkBuffer vertexBuffers[] = { m_vertexBuffer };
	VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
	vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);

	for (const auto& inst : dynamicInstances) {
		auto allocIt = m_meshAllocations.find(inst.meshHash);
		if (allocIt == m_meshAllocations.end() || allocIt->second.subMeshes.empty()) continue;

		const auto& sub = allocIt->second.subMeshes[0]; // Assuming primary submesh

		PushConstants constants{};
		constants.modelMatrix = inst.transform;
		constants.textureId = sub.textureId;
		constants.normalTextureId = sub.normalTextureId;

		vkCmdPushConstants(commandBuffer, pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(PushConstants), &constants);

		vkCmdDrawIndexed(commandBuffer, sub.indexCount, 1,
			sub.firstIndex + allocIt->second.firstIndex,
			sub.vertexOffset + allocIt->second.vertexOffset, 0);
	}
}

void StaticMeshRenderer::Cull(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos, const glm::mat4& viewProj, const glm::vec2& hzbSize, 
    uint32_t currentFrameIndex, uint32_t& outCulledCount, uint32_t& outVertexCount, uint32_t& outIndexCount)
{
	if (m_cullInstances.empty() ||
		m_cullPipeline == VK_NULL_HANDLE) {
		return;
	}

	if (m_lastBatchCount[currentFrameIndex] > 0) {
		uint32_t drawnInstances = 0;
		auto* readbackCmds = static_cast<VkDrawIndexedIndirectCommand*>(m_mappedIndirectReadback[currentFrameIndex]);

		for (uint32_t i = 0; i < m_lastBatchCount[currentFrameIndex]; ++i) {
			drawnInstances += readbackCmds[i].instanceCount;
		}

		if (m_lastSubmittedInstances[currentFrameIndex] >= drawnInstances) {
			outCulledCount += (m_lastSubmittedInstances[currentFrameIndex] - drawnInstances);
		}
	}

	m_lastSubmittedInstances[currentFrameIndex] = static_cast<uint32_t>(m_cullInstances.size());
	m_lastBatchCount[currentFrameIndex] = static_cast<uint32_t>(m_indirectBatches.size());

	const VkDeviceSize inputOffset =
		static_cast<VkDeviceSize>(currentFrameIndex) *
		m_cullInputStride;

	const VkDeviceSize inputSize =
		static_cast<VkDeviceSize>(m_cullInstances.size()) *
		sizeof(StaticInstanceCullData);

	auto* frameInputData =
		reinterpret_cast<uint8_t*>(m_mappedCullInput) +
		inputOffset;

	memcpy(
		frameInputData,
		m_cullInstances.data(),
		static_cast<size_t>(inputSize));

	std::vector<VkDrawIndexedIndirectCommand> commands;
	commands.reserve(m_indirectBatches.size());

	for (const StaticIndirectBatch& batch : m_indirectBatches) {
		commands.push_back(batch.command);

		outVertexCount += batch.command.indexCount * batch.sourceCount;
		outIndexCount += batch.command.indexCount * batch.sourceCount;
	}

	vkCmdUpdateBuffer(commandBuffer, m_indirectCommandBuffers[currentFrameIndex], 0,
		static_cast<VkDeviceSize>(commands.size() * sizeof(VkDrawIndexedIndirectCommand)), commands.data());

	VkBufferMemoryBarrier inputBarrier{};
	inputBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	inputBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
	inputBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	inputBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	inputBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	inputBarrier.buffer = m_cullInputBuffer;
	inputBarrier.offset = inputOffset;
	inputBarrier.size = inputSize;

	vkCmdPipelineBarrier(
		commandBuffer,
		VK_PIPELINE_STAGE_HOST_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0,
		0, nullptr,
		1, &inputBarrier,
		0, nullptr);

	VkMemoryBarrier commandResetBarrier{};
	commandResetBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	commandResetBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	commandResetBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

	vkCmdPipelineBarrier(
		commandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0,
		1, &commandResetBarrier,
		0, nullptr,
		0, nullptr);

	StaticCullPush push{};
	push.viewProj = viewProj;
	push.cameraPos = cameraPos;
	push.totalInstances = static_cast<uint32_t>(m_cullInstances.size());
	push.maxDistance = g_Settings.GetStaticFadeEnd();
	push.hzbSize = hzbSize;

	glm::vec3 axisX = glm::vec3(viewProj[0][0], viewProj[1][0], viewProj[2][0]);
	glm::vec3 axisY = glm::vec3(viewProj[0][1], viewProj[1][1], viewProj[2][1]);
	glm::vec3 axisZ = glm::vec3(viewProj[0][2], viewProj[1][2], viewProj[2][2]);
	glm::vec3 axisW = glm::vec3(viewProj[0][3], viewProj[1][3], viewProj[2][3]);

	// 2. Pre-calculate the expensive square roots on the CPU!
	glm::vec4 axisLengths(
		glm::length(axisX),
		glm::length(axisY),
		glm::length(axisZ),
		glm::length(axisW)
	);

	push.axisLengths = axisLengths;

	vkCmdBindPipeline(
		commandBuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		m_cullPipeline);

	vkCmdBindDescriptorSets(
		commandBuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		m_cullPipelineLayout,
		0,
		1,
		&m_cullDescriptorSets[currentFrameIndex],
		0,
		nullptr);

	vkCmdPushConstants(
		commandBuffer,
		m_cullPipelineLayout,
		VK_SHADER_STAGE_COMPUTE_BIT,
		0,
		sizeof(StaticCullPush),
		&push);

	vkCmdDispatch(commandBuffer, (push.totalInstances + 127) / 128, 1, 1);

	VkMemoryBarrier drawBarrier{};
	drawBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	drawBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	drawBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

	vkCmdPipelineBarrier(
		commandBuffer,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		// Add TRANSFER_BIT pipeline stage
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 1, &drawBarrier, 0, nullptr, 0, nullptr);

	if (!m_indirectBatches.empty()) {
		VkBufferCopy copyRegion{};
		copyRegion.size = sizeof(VkDrawIndexedIndirectCommand) * m_indirectBatches.size();
		vkCmdCopyBuffer(commandBuffer, m_indirectCommandBuffers[currentFrameIndex], m_indirectReadbackBuffers[currentFrameIndex], 1, &copyRegion);
	}
}

void StaticMeshRenderer::CreateCullPipeline() {
	VkDescriptorSetLayoutBinding bindings[] = {
		{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr } 
	};
	VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 4, bindings };
	if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_cullDescriptorSetLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to create static cull descriptor layout");
	VkPushConstantRange range{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(StaticCullPush) };
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_cullDescriptorSetLayout, 1, &range };
	if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_cullPipelineLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to create static cull pipeline layout");
	auto code = VulkanRenderer::ReadFile("shaders/static_cull_comp.spv");
	VkShaderModule module = VulkanRenderer::CreateShaderModule(m_device, code);
	VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr };
	VkComputePipelineCreateInfo info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, stage, m_cullPipelineLayout, VK_NULL_HANDLE, 0 };
	if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &m_cullPipeline) != VK_SUCCESS)
		throw std::runtime_error("Failed to create static cull compute pipeline");
	vkDestroyShaderModule(m_device, module, nullptr);
}

void StaticMeshRenderer::UpdateCullDescriptors() {
	VkDescriptorPoolSize poolSizes[2]{};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSizes[0].descriptorCount = 3 * m_framesInFlight;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[1].descriptorCount = m_framesInFlight;

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = m_framesInFlight;
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = poolSizes;

	if (vkCreateDescriptorPool(
		m_device,
		&poolInfo,
		nullptr,
		&m_cullDescriptorPool) != VK_SUCCESS) {
		throw std::runtime_error(
			"Failed to create static cull descriptor pool");
	}

	std::vector<VkDescriptorSetLayout> layouts(
		m_framesInFlight,
		m_cullDescriptorSetLayout);

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType =
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_cullDescriptorPool;
	allocInfo.descriptorSetCount = m_framesInFlight;
	allocInfo.pSetLayouts = layouts.data();

	if (vkAllocateDescriptorSets(
		m_device,
		&allocInfo,
		m_cullDescriptorSets.data()) != VK_SUCCESS) {
		throw std::runtime_error(
			"Failed to allocate static cull descriptor sets");
	}

	for (uint32_t i = 0; i < m_framesInFlight; ++i) {
		VkDescriptorBufferInfo inputInfo{};
		inputInfo.buffer = m_cullInputBuffer;
		inputInfo.offset =
			static_cast<VkDeviceSize>(i) *
			m_cullInputStride;
		inputInfo.range = m_cullInputStride;

		VkDescriptorBufferInfo outputInfo{};
		outputInfo.buffer = m_visibleInstanceBuffers[i];
		outputInfo.offset = 0;
		outputInfo.range = VK_WHOLE_SIZE;

		VkDescriptorBufferInfo commandInfo{};
		commandInfo.buffer = m_indirectCommandBuffers[i];
		commandInfo.offset = 0;
		commandInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet writes[3]{};

		writes[0].sType =
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = m_cullDescriptorSets[i];
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType =
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[0].pBufferInfo = &inputInfo;

		writes[1].sType =
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = m_cullDescriptorSets[i];
		writes[1].dstBinding = 1;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType =
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[1].pBufferInfo = &outputInfo;

		writes[2].sType =
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[2].dstSet = m_cullDescriptorSets[i];
		writes[2].dstBinding = 2;
		writes[2].descriptorCount = 1;
		writes[2].descriptorType =
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[2].pBufferInfo = &commandInfo;

		vkUpdateDescriptorSets(
			m_device,
			3,
			writes,
			0,
			nullptr);
	}
}

void StaticMeshRenderer::UpdateHZBDescriptor(VkImageView hzbView, VkSampler hzbSampler) {
	if (m_cullDescriptorSets[0] == VK_NULL_HANDLE) return;

	std::vector<VkWriteDescriptorSet> writes;
	std::vector<VkDescriptorImageInfo> imageInfos(m_framesInFlight);

	for (uint32_t i = 0; i < m_framesInFlight; ++i) {
		imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfos[i].imageView = hzbView;
		imageInfos[i].sampler = hzbSampler;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = m_cullDescriptorSets[i];
		write.dstBinding = 3;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.descriptorCount = 1;
		write.pImageInfo = &imageInfos[i];

		writes.push_back(write);
	}
	vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}