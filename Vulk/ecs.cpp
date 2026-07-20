#pragma once

#include <vector>
#include <string>
#include <memory>
#include "scene_types.h"
#include "ecs.h"

class Scene {
private:
    Registry m_registry;

    // Cached vector matching the original structural layout required by VulkanRenderer
    mutable std::vector<MeshInstance> m_cachedInstances;

public:
    Registry& GetRegistry() { return m_registry; }
    const Registry& GetRegistry() const { return m_registry; }

    Entity CreateRenderableEntity(const std::string& meshName, uint32_t albedoId = 0, uint32_t normalId = 0, MeshType type = MeshType::Static) {
        Entity entity = m_registry.CreateEntity();

        TransformComponent transform;
        m_registry.AddComponent<TransformComponent>(entity, transform);

        RenderComponent render;
        render.meshName = meshName;
        render.albedoTextureId = albedoId;
        render.normalTextureId = normalId;
        render.type = type;
        m_registry.AddComponent<RenderComponent>(entity, render);

        return entity;
    }

    void RemoveChunk(int64_t key) {
        // Query our component pools to find entities tagged with this specific chunk key
        const auto& renderables = m_registry.View<RenderComponent>();

        // Gather entities first to avoid modifying the array while iterating
        std::vector<Entity> toDestroy;
        for (Entity entity : renderables) {
            if (m_registry.HasComponent<TransformComponent>(entity)) {
                // If you later add a chunkKey variable to components, check it here
            }
        }

        for (Entity entity : toDestroy) {
            m_registry.DestroyEntity(entity);
        }
    }

    const std::vector<MeshInstance>& GetInstances() const {
        m_cachedInstances.clear();

        // This is where the cache speed shows up! 
        // We get a references to a completely contiguous layout of active render components.
        Registry& reg = const_cast<Registry&>(m_registry);
        const auto& renderableEntities = reg.View<RenderComponent>();

        for (Entity entity : renderableEntities) {
            const auto& render = reg.GetComponent<RenderComponent>(entity);
            if (!render.isVisible) continue;

            auto& transform = reg.GetComponent<TransformComponent>(entity);
            transform.UpdateMatrix(); // Only updates if something shifted position

            MeshInstance inst{};
            inst.meshName = render.meshName;
            inst.transform = transform.modelMatrix;
            inst.objectId = entity;
            inst.type = render.type;
            inst.chunkKey = -1;

            m_cachedInstances.push_back(inst);
        }

        return m_cachedInstances;
    }

    // Keep light API uniform or change to component lights later if needed
    // For now, keeping it backward compatible with your current renderer layout
    std::vector<SceneLight> lights;
    mutable bool hasModifiedLights = false;

    void AddLight(const SceneLight& light) { lights.push_back(light); hasModifiedLights = true; }
    bool HasModifiedLights() const { return hasModifiedLights; }
    void ClearModifiedLightsFlag() const { hasModifiedLights = false; }
    const std::vector<SceneLight>& GetLights() const { return lights; }

    void Clear() {
        m_registry.Clear();
        lights.clear();
        m_cachedInstances.clear();
    }
};