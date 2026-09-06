#pragma once

#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include "mesh_types.h"
#include "ecs.h"
#include "light.h"

struct ChunkPropComponent {
    int64_t chunkKey = -1;
};

class Scene {
private:
    Registry m_registry;
    std::vector<Light> lights;
    mutable bool hasModifiedLights = false;

    // Caching mechanism to stop CPU melting
    mutable std::vector<MeshInstance> m_cachedInstances;
    mutable bool m_isDirty = true;

public:
    Registry& GetRegistry() { return m_registry; }
    const Registry& GetRegistry() const { return m_registry; }

    // Call this whenever chunks load or unload to rebuild the renderer arrays!
    void MarkDirty() const { m_isDirty = true; }
    bool NeedsRendererUpdate() const {
        return m_isDirty;
    }

    void AddInstance(const std::string& meshName, const glm::mat4& transform, uint32_t objectId) {
        Entity entity = m_registry.CreateEntity();

        TransformComponent tComp;
        tComp.modelMatrix = transform;
        tComp.position = glm::vec3(transform[3]);
        tComp.isDirty = false;
        m_registry.AddComponent<TransformComponent>(entity, tComp);

        RenderComponent rComp;
        rComp.meshName = meshName;
        rComp.type = MeshType::Static;
        rComp.isVisible = true;
        m_registry.AddComponent<RenderComponent>(entity, rComp);

        m_isDirty = true;
    }

    void RemoveChunk(int64_t key) {
        auto chunkProps = m_registry.QueryGroup<ChunkPropComponent>();
        std::vector<Entity> toDestroy;

        for (Entity e : chunkProps) {
            if (m_registry.GetComponent<ChunkPropComponent>(e).chunkKey == key) toDestroy.push_back(e);
        }

        if (toDestroy.empty()) return;

        for (Entity e : toDestroy) m_registry.DestroyEntity(e);
        m_isDirty = true;
    }

    const std::vector<MeshInstance>& GetInstances() const {
        // INSTANT RETURN: If trees haven't loaded or unloaded, skip the 300,000 loops!
        if (!m_isDirty) return m_cachedInstances;

        m_cachedInstances.clear();
        Registry& reg = const_cast<Registry&>(m_registry);
        auto renderableGroup = reg.QueryGroup<RenderComponent, TransformComponent>();

        for (Entity entity : renderableGroup) {
            const auto& render = reg.GetComponent<RenderComponent>(entity);
            if (!render.isVisible) continue;

            auto& transform = reg.GetComponent<TransformComponent>(entity);
            transform.UpdateMatrix(reg);

            MeshInstance inst{};
            inst.meshName = render.meshName;
            inst.transform = transform.modelMatrix;
            inst.objectId = entity;
            inst.type = render.type;
            inst.isInstanced = render.isInstanced;

            // Grab the chunk key so the renderer can group trees by their chunk
            if (reg.HasComponent<ChunkPropComponent>(entity)) {
                inst.chunkKey = reg.GetComponent<ChunkPropComponent>(entity).chunkKey;
            }
            else {
                inst.chunkKey = -1;
            }

            m_cachedInstances.push_back(inst);
        }

        m_isDirty = false;
        return m_cachedInstances;
    }

    void AddLight(const Light& light) { lights.push_back(light); hasModifiedLights = true; }
    void SetLights(const std::vector<Light>& newLights) {
        lights = newLights;
        hasModifiedLights = true; // Flags the VulkanRenderer to upload the new buffer
    }
    bool HasModifiedLights() const { return hasModifiedLights; }
    void ClearModifiedLightsFlag() const { hasModifiedLights = false; }
    const std::vector<Light>& GetLights() const { return lights; }

    void Clear() {
        m_registry.Clear();
        lights.clear();
        m_cachedInstances.clear();
        m_isDirty = true;
    }
};