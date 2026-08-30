#include "renderer_static.h"
#include "renderer.h"
#include "asset_manager.h"
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <glm/gtx/norm.hpp>

void StaticMeshRenderer::Init(VkDevice device, VulkanRenderer* renderer) {
    m_device = device;
    m_renderer = renderer;

    m_maxVertices = MAX_GLOBAL_VERTICES;
    m_maxIndices = MAX_GLOBAL_INDICES;

    VkDeviceSize vertexSize = sizeof(ModelVertex) * m_maxVertices;
    VkDeviceSize indexSize = sizeof(uint32_t) * m_maxIndices;

    m_renderer->CreateBuffer(vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vertexBuffer, m_vertexMemory);

    m_renderer->CreateBuffer(indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indexBuffer, m_indexMemory);

    // Provision the Instance Buffer (Host Visible for high-speed CPU memory mapping)
    constexpr VkDeviceSize FRAMES_IN_FLIGHT = 3;
    VkDeviceSize totalInstanceBytes = sizeof(InstanceData) * m_maxInstances * FRAMES_IN_FLIGHT;
    m_renderer->CreateBuffer(totalInstanceBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_instanceBuffer, m_instanceMemory);

    std::cout << "[StaticMeshRenderer] Initialized geometry pools and Instancing Pipeline.\n";
}

void StaticMeshRenderer::Cleanup() {
    for (size_t i = 0; i < FRAMES_IN_FLIGHT_COUNT; ++i) {
        if (m_queryPools[i] != VK_NULL_HANDLE) {
            vkDestroyQueryPool(m_device, m_queryPools[i], nullptr);
            m_queryPools[i] = VK_NULL_HANDLE;
        }
    }
    m_renderer->DestroyBuffer(m_vertexBuffer, m_vertexMemory);
    m_renderer->DestroyBuffer(m_indexBuffer, m_indexMemory);
    m_renderer->DestroyBuffer(m_instanceBuffer, m_instanceMemory);
}

void StaticMeshRenderer::ResizeBuffers(uint32_t requiredVertices, uint32_t requiredIndices) {
    vkDeviceWaitIdle(m_device);
    m_renderer->DestroyBuffer(m_vertexBuffer, m_vertexMemory);
    m_renderer->DestroyBuffer(m_indexBuffer, m_indexMemory);

    m_maxVertices = requiredVertices;
    m_maxIndices = requiredIndices;

    m_renderer->CreateBuffer(sizeof(ModelVertex) * m_maxVertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vertexBuffer, m_vertexMemory);
    m_renderer->CreateBuffer(sizeof(uint32_t) * m_maxIndices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indexBuffer, m_indexMemory);
}

void StaticMeshRenderer::UploadUniqueMeshes(const std::unordered_set<std::string>& uniqueMeshNames) {
    std::vector<ModelVertex> allVerts;
    std::vector<uint32_t> allIndices;
    m_meshAllocations.clear();

    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;

    for (const auto& name : uniqueMeshNames) {
        MeshAsset* mesh = g_AssetManager.GetMesh(name);
        if (!mesh) continue;

        allVerts.insert(allVerts.end(), mesh->vertices.begin(), mesh->vertices.end());
        allIndices.insert(allIndices.end(), mesh->indices.begin(), mesh->indices.end());

        MeshBufferAllocation alloc;
        alloc.firstIndex = indexOffset;
        alloc.vertexOffset = vertexOffset;
        alloc.maxBoundingRadius = 0.0f;

        for (size_t subIdx = 0; subIdx < mesh->subMeshes.size(); ++subIdx) {
            SubMesh sub = mesh->subMeshes[subIdx];
            sub.textureId = g_AssetManager.GetTextureId(mesh->materialTextures[subIdx]);
            sub.normalTextureId = g_AssetManager.GetNormalTextureId(mesh->normalMapTextures[subIdx]);

            alloc.maxBoundingRadius = std::max(alloc.maxBoundingRadius, sub.boundingRadiusLocal);
            alloc.subMeshes.push_back(sub);
        }

        m_meshAllocations[name] = alloc;
        vertexOffset += static_cast<uint32_t>(mesh->vertices.size());
        indexOffset += static_cast<uint32_t>(mesh->indices.size());
    }

    if (allVerts.empty() || allIndices.empty()) return;

    if (allVerts.size() > m_maxVertices || allIndices.size() > m_maxIndices) {
        // CLEANUP FIX: Explicit template and casts for std::max so the compiler doesn't throw C2672
        uint32_t targetVerts = std::max<uint32_t>(static_cast<uint32_t>(allVerts.size() * 2), static_cast<uint32_t>(m_maxVertices));
        uint32_t targetIndices = std::max<uint32_t>(static_cast<uint32_t>(allIndices.size() * 2), static_cast<uint32_t>(m_maxIndices));
        ResizeBuffers(targetVerts, targetIndices);
    }

    VkBuffer stagingVert, stagingIndex;
    VkDeviceMemory stagingVertMem, stagingIndexMem;
    m_renderer->CreateBuffer(sizeof(ModelVertex) * allVerts.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingVert, stagingVertMem);
    m_renderer->CreateBuffer(sizeof(uint32_t) * allIndices.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingIndex, stagingIndexMem);

    void* data;
    vkMapMemory(m_device, stagingVertMem, 0, sizeof(ModelVertex) * allVerts.size(), 0, &data);
    memcpy(data, allVerts.data(), sizeof(ModelVertex) * allVerts.size());
    vkUnmapMemory(m_device, stagingVertMem);

    vkMapMemory(m_device, stagingIndexMem, 0, sizeof(uint32_t) * allIndices.size(), 0, &data);
    memcpy(data, allIndices.data(), sizeof(uint32_t) * allIndices.size());
    vkUnmapMemory(m_device, stagingIndexMem);

    VkCommandBuffer cmd = m_renderer->BeginSingleTimeCommands();
    VkBufferCopy vCopy{ 0, 0, sizeof(ModelVertex) * allVerts.size() };
    vkCmdCopyBuffer(cmd, stagingVert, m_vertexBuffer, 1, &vCopy);
    VkBufferCopy iCopy{ 0, 0, sizeof(uint32_t) * allIndices.size() };
    vkCmdCopyBuffer(cmd, stagingIndex, m_indexBuffer, 1, &iCopy);
    m_renderer->EndSingleTimeCommands(cmd);

    m_renderer->DestroyBuffer(stagingVert, stagingVertMem);
    m_renderer->DestroyBuffer(stagingIndex, stagingIndexMem);
}

void StaticMeshRenderer::UpdateScene(const Scene& scene) {
    const auto& instances = scene.GetInstances();

    std::unordered_set<std::string> uniqueMeshNames;
    for (const auto& inst : instances) uniqueMeshNames.insert(inst.meshName);

    bool geometryChanged = (uniqueMeshNames.size() != m_meshAllocations.size());
    if (!geometryChanged) {
        for (const auto& name : uniqueMeshNames) {
            if (m_meshAllocations.find(name) == m_meshAllocations.end()) { geometryChanged = true; break; }
        }
    }

    if (geometryChanged) UploadUniqueMeshes(uniqueMeshNames);

    m_staticDrawList.clear();
    m_sceneObjects.clear();
    m_subMeshes.clear();
    m_instancedGroups.clear();

    for (const auto& inst : instances) {
        if (inst.isInstanced) {
            // Group the tree into its specific chunk bucket
            auto& bucket = m_instancedGroups[inst.meshName].chunkBuckets[inst.chunkKey];
            bucket.transforms.push_back(inst.transform);

            // Calculate the 512x512 mathematical bounds of the chunk dynamically
            if (bucket.chunkRadius == 0.0f) {
                int cx = static_cast<int>(inst.chunkKey >> 32);
                int cz = static_cast<int>(inst.chunkKey & 0xFFFFFFFF);
                bucket.chunkCenter = glm::vec3(cx * 512.0f + 256.0f, 0.0f, cz * 512.0f + 256.0f);
                bucket.chunkRadius = 512.0f * 1.41421356f * 0.5f; // Math hypotenuse for box radius
            }
        }
        else {
            // Sponza and static props 
            auto& alloc = m_meshAllocations[inst.meshName];
            SceneObject obj;
            obj.modelMatrix = inst.transform;
            obj.objectId = inst.objectId;

            uint32_t objIdx = static_cast<uint32_t>(m_sceneObjects.size());
            m_sceneObjects.push_back(obj);

            for (const auto& sub : alloc.subMeshes) {
                uint32_t subIdx = static_cast<uint32_t>(m_subMeshes.size());
                SubMesh activeSub = sub;
                activeSub.vertexOffset += alloc.vertexOffset;
                activeSub.firstIndex += alloc.firstIndex;
                m_subMeshes.push_back(activeSub);

                DrawEntry entry;
                entry.objectIndex = objIdx;
                entry.subMeshIndex = subIdx;

                float maxS = std::max({ glm::length(glm::vec3(inst.transform[0])), glm::length(glm::vec3(inst.transform[1])), glm::length(glm::vec3(inst.transform[2])) });
                entry.cachedMaxScale = maxS;
                m_staticDrawList.push_back(entry);
            }
        }
    }
}

void StaticMeshRenderer::Draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
    VkDescriptorSet descriptorSet, const glm::vec3& cameraPos,
    const std::array<FrustumPlane, 6>& frustumPlanes, uint32_t currentFrameIndex,
    VkPipeline staticPipeline, VkPipeline instancedPipeline,
    uint32_t& outDrawCalls, uint32_t& outCulledCount,
    uint32_t& outVertexCount, uint32_t& outIndexCount) {

    // 1. UNIQUE MESHES (Sponza, Players, etc.)
    if (!m_staticDrawList.empty()) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, staticPipeline);

        VkBuffer vertexBuffers[] = { m_vertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        for (const auto& entry : m_staticDrawList) {
            const auto& obj = m_sceneObjects[entry.objectIndex];
            const auto& sub = m_subMeshes[entry.subMeshIndex];

            glm::vec3 worldCenter = glm::vec3(obj.modelMatrix * glm::vec4(sub.boundingCenterLocal, 1.0f));
            float worldRadius = sub.boundingRadiusLocal * entry.cachedMaxScale;

            if (!m_renderer->IsWorldSphereInFrustum(worldCenter, worldRadius)) {
                outCulledCount++; continue;
            }

            PushConstants constants{};
            constants.modelMatrix = obj.modelMatrix;
            constants.textureId = sub.textureId;
            constants.normalTextureId = sub.normalTextureId;
            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &constants);
            vkCmdDrawIndexed(commandBuffer, sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);
            outDrawCalls++; outVertexCount += sub.indexCount; outIndexCount += sub.indexCount;
        }
    }

    // 2. HARDWARE INSTANCED MESHES (Forests, Foliage)
    if (!m_instancedGroups.empty()) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, instancedPipeline);

        VkBuffer vertexBuffers[] = { m_vertexBuffer, m_instanceBuffer };

        VkDeviceSize frameInstanceStride = sizeof(InstanceData) * m_maxInstances;
        VkDeviceSize frameByteOffset = currentFrameIndex * frameInstanceStride;

        VkDeviceSize offsets[] = { 0, frameByteOffset };
        vkCmdBindVertexBuffers(commandBuffer, 0, 2, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        InstanceData* mappedData;
        vkMapMemory(m_device, m_instanceMemory, frameByteOffset, frameInstanceStride, 0, (void**)&mappedData);

        uint32_t currentInstanceOffset = 0;
        const float maxFoliageDistSq = 4000.0f * 4000.0f;

        for (const auto& pair : m_instancedGroups) {
            const std::string& meshName = pair.first;
            const auto& group = pair.second;
            const auto& alloc = m_meshAllocations[meshName];

            for (const auto& chunkPair : group.chunkBuckets) {
                const auto& bucket = chunkPair.second;

                if (!m_renderer->IsWorldSphereInFrustum(bucket.chunkCenter, bucket.chunkRadius)) {
                    outCulledCount += static_cast<uint32_t>(bucket.transforms.size());
                    continue; 
                }

                uint32_t visibleCount = 0;

                for (const auto& transform : bucket.transforms) {
                    glm::vec3 pos = glm::vec3(transform[3]);

                    if (glm::distance2(pos, cameraPos) > maxFoliageDistSq) {
                        outCulledCount++;
                        continue;
                    }

                    if (currentInstanceOffset + visibleCount >= m_maxInstances) {
                        break;
                    }

                    mappedData[currentInstanceOffset + visibleCount].modelMatrix = transform;
                    visibleCount++;
                }

                if (visibleCount > 0) {
                    for (const auto& sub : alloc.subMeshes) {
                        PushConstants constants{};
                        constants.textureId = sub.textureId;
                        constants.normalTextureId = sub.normalTextureId;
                        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &constants);

                        vkCmdDrawIndexed(commandBuffer, sub.indexCount, visibleCount, sub.firstIndex + alloc.firstIndex, sub.vertexOffset + alloc.vertexOffset, currentInstanceOffset);
                        outDrawCalls++;
                        outVertexCount += (sub.indexCount * visibleCount);
                        outIndexCount += (sub.indexCount * visibleCount);
                    }
                    currentInstanceOffset += visibleCount;
                }
            }
        }
        vkUnmapMemory(m_device, m_instanceMemory);
    }
}