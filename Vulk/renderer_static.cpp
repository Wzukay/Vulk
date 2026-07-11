#include "renderer_static.h"
#include "renderer.h"
#include "assetManager.h"
#include <iostream>
#include <algorithm>
#include <glm/gtx/norm.hpp>

void StaticMeshRenderer::Init(VkDevice device, VulkanRenderer* renderer) {
    m_device = device;
    m_renderer = renderer;

    VkDeviceSize vertexSize = sizeof(ModelVertex) * MAX_GLOBAL_VERTICES;
    VkDeviceSize indexSize = sizeof(uint32_t) * MAX_GLOBAL_INDICES;

    m_renderer->CreateBuffer(vertexSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_vertexBuffer, m_vertexMemory);

    m_renderer->CreateBuffer(indexSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_indexBuffer, m_indexMemory);

    std::cout << "[StaticMeshRenderer] Initialized with " << MAX_GLOBAL_VERTICES << " vertices, "
        << MAX_GLOBAL_INDICES << " indices.\n";
}

void StaticMeshRenderer::Cleanup() {
    m_renderer->DestroyBuffer(m_vertexBuffer, m_vertexMemory);
    m_renderer->DestroyBuffer(m_indexBuffer, m_indexMemory);
    m_sceneObjects.clear();
    m_subMeshes.clear();
    m_staticDrawList.clear();
    m_visibleStaticDrawList.clear();
    m_lastInstanceMeshNames.clear();
    m_objectDrawEntryIndices.clear();
}

void StaticMeshRenderer::UpdateScene(const Scene& scene) {
    const auto& instances = scene.GetInstances();

    bool geometryChanged = (instances.size() != m_lastInstanceMeshNames.size());
    if (!geometryChanged) {
        for (size_t i = 0; i < instances.size(); ++i) {
            if (instances[i].meshName != m_lastInstanceMeshNames[i]) {
                geometryChanged = true;
                break;
            }
        }
    }

    if (geometryChanged) {
        // Estimate sizes for reservation
        size_t estimatedVertCount = 0;
        size_t estimatedIndexCount = 0;
        size_t estimatedSubMeshCount = 0;
        for (const auto& inst : instances) {
            MeshAsset* mesh = g_AssetManager.GetMesh(inst.meshName);
            if (mesh) {
                estimatedVertCount += mesh->vertices.size();
                estimatedIndexCount += mesh->indices.size();
                estimatedSubMeshCount += mesh->subMeshes.size();
            }
        }

        std::vector<ModelVertex> allVerts;
        std::vector<uint32_t> allIndices;
        std::vector<SubMesh> allSubMeshes;
        std::vector<SceneObject> objects;

        allVerts.reserve(estimatedVertCount);
        allIndices.reserve(estimatedIndexCount);
        allSubMeshes.reserve(estimatedSubMeshCount);
        objects.reserve(instances.size());
        m_staticDrawList.reserve(estimatedSubMeshCount);
        m_objectDrawEntryIndices.reserve(instances.size());

        m_staticDrawList.clear();
        m_lastInstanceMeshNames.clear();
        m_objectDrawEntryIndices.clear();
        m_objectDrawEntryIndices.resize(instances.size());

        uint32_t vertexOffset = 0;
        uint32_t indexOffset = 0;

        for (size_t instIdx = 0; instIdx < instances.size(); ++instIdx) {
            const auto& inst = instances[instIdx];
            m_lastInstanceMeshNames.push_back(inst.meshName);

            MeshAsset* mesh = g_AssetManager.GetMesh(inst.meshName);
            if (!mesh) {
                std::cerr << "[StaticMeshRenderer] Mesh not found: " << inst.meshName << "\n";
                continue;
            }

            allVerts.insert(allVerts.end(), mesh->vertices.begin(), mesh->vertices.end());
            allIndices.insert(allIndices.end(), mesh->indices.begin(), mesh->indices.end());

            SceneObject obj;
            obj.modelMatrix = inst.transform;
            obj.objectId = inst.objectId;
            obj.type = inst.type;
            obj.firstSubMesh = static_cast<uint32_t>(allSubMeshes.size());
            obj.subMeshCount = static_cast<uint32_t>(mesh->subMeshes.size());

            for (size_t subIdx = 0; subIdx < mesh->subMeshes.size(); ++subIdx) {
                const auto& sub = mesh->subMeshes[subIdx];
                SubMesh newSub = sub;
                newSub.vertexOffset += vertexOffset;
                newSub.firstIndex += indexOffset;

                const std::string& texPath = mesh->materialTextures[subIdx];
                newSub.textureId = g_AssetManager.GetTextureId(texPath);

                const std::string& normalPath = mesh->normalMapTextures[subIdx];
                newSub.normalTextureId = g_AssetManager.GetNormalTextureId(normalPath);

                allSubMeshes.push_back(newSub);
            }

            objects.push_back(obj);

            float scaleX = glm::length(glm::vec3(obj.modelMatrix[0]));
            float scaleY = glm::length(glm::vec3(obj.modelMatrix[1]));
            float scaleZ = glm::length(glm::vec3(obj.modelMatrix[2]));
            float maxScale = std::max({ scaleX, scaleY, scaleZ });

            uint32_t objIdx = static_cast<uint32_t>(objects.size()) - 1;
            for (uint32_t sub = obj.firstSubMesh; sub < obj.firstSubMesh + obj.subMeshCount; ++sub) {
                DrawEntry entry{};
                entry.objectIndex = objIdx;
                entry.subMeshIndex = sub;
                entry.cachedMaxScale = maxScale;

                m_objectDrawEntryIndices[instIdx].push_back(static_cast<uint32_t>(m_staticDrawList.size()));
                m_staticDrawList.push_back(entry);
            }

            vertexOffset += static_cast<uint32_t>(mesh->vertices.size());
            indexOffset += static_cast<uint32_t>(mesh->indices.size());
        }

        UploadStaticSceneData(allVerts, allIndices);

        m_sceneObjects = std::move(objects);
        m_subMeshes = std::move(allSubMeshes);
    }
    else {
        // ---- Cheap path: just refresh transforms ----
        for (size_t i = 0; i < instances.size() && i < m_sceneObjects.size(); ++i) {
            const glm::mat4& xform = instances[i].transform;
            m_sceneObjects[i].modelMatrix = xform;

            float scaleX = glm::length(glm::vec3(xform[0]));
            float scaleY = glm::length(glm::vec3(xform[1]));
            float scaleZ = glm::length(glm::vec3(xform[2]));
            float maxScale = std::max({ scaleX, scaleY, scaleZ });

            for (uint32_t drawIdx : m_objectDrawEntryIndices[i]) {
                m_staticDrawList[drawIdx].cachedMaxScale = maxScale;
            }
        }
    }
}

void StaticMeshRenderer::UploadStaticSceneData(const std::vector<ModelVertex>& verts,
    const std::vector<uint32_t>& idxs) {
    if (verts.empty() || idxs.empty()) return;

    if (verts.size() > m_maxVertices || idxs.size() > m_maxIndices) {
        throw std::runtime_error("Static scene data exceeds pre‑allocated buffer size!");
    }

    VkDeviceSize vertexSize = sizeof(ModelVertex) * verts.size();
    VkDeviceSize indexSize = sizeof(uint32_t) * idxs.size();

    // Staging buffers
    VkBuffer stagingVert = VK_NULL_HANDLE;
    VkDeviceMemory stagingVertMem = VK_NULL_HANDLE;
    VkBuffer stagingIndex = VK_NULL_HANDLE;
    VkDeviceMemory stagingIndexMem = VK_NULL_HANDLE;

    try {
        m_renderer->CreateBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingVert, stagingVertMem);
        void* data;
        vkMapMemory(m_device, stagingVertMem, 0, vertexSize, 0, &data);
        memcpy(data, verts.data(), vertexSize);
        vkUnmapMemory(m_device, stagingVertMem);

        m_renderer->CreateBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingIndex, stagingIndexMem);
        vkMapMemory(m_device, stagingIndexMem, 0, indexSize, 0, &data);
        memcpy(data, idxs.data(), indexSize);
        vkUnmapMemory(m_device, stagingIndexMem);

        VkCommandBuffer cmd = m_renderer->BeginSingleTimeCommands();
        VkBufferCopy vertCopy{ 0, 0, vertexSize };
        vkCmdCopyBuffer(cmd, stagingVert, m_vertexBuffer, 1, &vertCopy);
        VkBufferCopy indCopy{ 0, 0, indexSize };
        vkCmdCopyBuffer(cmd, stagingIndex, m_indexBuffer, 1, &indCopy);
        m_renderer->EndSingleTimeCommands(cmd);

        m_renderer->DestroyBuffer(stagingVert, stagingVertMem);
        m_renderer->DestroyBuffer(stagingIndex, stagingIndexMem);
    }
    catch (...) {
        m_renderer->DestroyBuffer(stagingVert, stagingVertMem);
        m_renderer->DestroyBuffer(stagingIndex, stagingIndexMem);
        throw;
    }
}

void StaticMeshRenderer::Draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
    VkDescriptorSet descriptorSet, const glm::vec3& cameraPos,
    const std::array<FrustumPlane, 6>& frustumPlanes,
    uint32_t& outDrawCalls, uint32_t& outCulledCount,
    uint32_t& outVertexCount, uint32_t& outIndexCount) {
    if (m_staticDrawList.empty()) return;

    // Frustum culling & sorting
    m_visibleStaticDrawList.clear();
    for (const DrawEntry& original : m_staticDrawList) {
        const auto& obj = m_sceneObjects[original.objectIndex];
        const auto& sub = m_subMeshes[original.subMeshIndex];

        float maxScale = original.cachedMaxScale;
        glm::vec3 worldCenter = glm::vec3(obj.modelMatrix * glm::vec4(sub.boundingCenterLocal, 1.0f));
        float worldRadius = sub.boundingRadiusLocal * maxScale;

        // Check frustum
        bool visible = true;
        for (const auto& plane : frustumPlanes) {
            if (glm::dot(plane.normal, worldCenter) + plane.distance + worldRadius < 0.0f) {
                visible = false;
                break;
            }
        }
        if (!visible) {
            outCulledCount++;
            continue;
        }

        DrawEntry entry = original;
        entry.distSq = glm::length2(worldCenter - cameraPos);
        m_visibleStaticDrawList.push_back(entry);
    }

    // Sort by distance (front-to-back)
    std::sort(m_visibleStaticDrawList.begin(), m_visibleStaticDrawList.end(),
        [](const DrawEntry& a, const DrawEntry& b) { return a.distSq < b.distSq; });

    // Bind vertex/index buffers
    VkBuffer vertexBuffers[] = { m_vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // Draw each visible sub‑mesh
    for (const auto& entry : m_visibleStaticDrawList) {
        const auto& obj = m_sceneObjects[entry.objectIndex];
        const auto& sub = m_subMeshes[entry.subMeshIndex];

        PushConstants constants{};
        constants.modelMatrix = obj.modelMatrix;
        constants.objectId = obj.objectId;
        constants.textureId = sub.textureId;
        constants.normalTextureId = sub.normalTextureId;
        constants.lodBlend = 0.0f; // not used for static meshes

        vkCmdPushConstants(commandBuffer, pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(PushConstants), &constants);

        vkCmdDrawIndexed(commandBuffer,
            sub.indexCount,
            1,
            sub.firstIndex,
            sub.vertexOffset,
            0);

        outDrawCalls++;
        outVertexCount += sub.indexCount;
        outIndexCount += sub.indexCount;
    }
}