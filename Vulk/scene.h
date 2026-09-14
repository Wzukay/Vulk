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

    mutable std::vector<MeshInstance> m_staticInstances;
    mutable std::vector<MeshInstance> m_dynamicInstances;
    mutable bool m_staticDirty = true;

public:
    Registry& GetRegistry() { return m_registry; }
    const Registry& GetRegistry() const { return m_registry; }

    bool NeedsStaticUpdate() const { return m_staticDirty; }
    void MarkStaticDirty() const { m_staticDirty = true; }

    void AddInstance(const std::string& meshName, const glm::mat4& transform, uint32_t objectId) {
        Entity entity = m_registry.CreateEntity();

        TransformComponent tComp;
        tComp.modelMatrix = transform;
        tComp.position = glm::vec3(transform[3]);
        tComp.isDirty = false;
        m_registry.AddComponent<TransformComponent>(entity, tComp);

        RenderComponent rComp;
        rComp.meshHash = StringHash::Hash(meshName);
        rComp.type = MeshType::Static;
        rComp.isVisible = true;
        m_registry.AddComponent<RenderComponent>(entity, rComp);

        m_staticDirty = true;
    }

    void RemoveChunk(int64_t key) {
        auto chunkProps = m_registry.QueryGroup<ChunkPropComponent>();
        std::vector<Entity> toDestroy;

        for (Entity e : chunkProps) {
            if (m_registry.GetComponent<ChunkPropComponent>(e).chunkKey == key) toDestroy.push_back(e);
        }

        if (toDestroy.empty()) return;

        for (Entity e : toDestroy) m_registry.DestroyEntity(e);
        m_staticDirty = true;
    }

    const std::vector<MeshInstance>& GetStaticInstances() const {
        if (!m_staticDirty) return m_staticInstances;

        m_staticInstances.clear();
        Registry& reg = const_cast<Registry&>(m_registry);
        auto renderableGroup = reg.QueryGroup<RenderComponent, TransformComponent>();

        for (Entity entity : renderableGroup) {
            const auto& render = reg.GetComponent<RenderComponent>(entity);
            if (!render.isVisible || render.type != MeshType::Static) continue;

            auto& transform = reg.GetComponent<TransformComponent>(entity);
            transform.UpdateMatrix(reg);

            MeshInstance inst{};
            inst.meshHash = render.meshHash;
            inst.transform = transform.modelMatrix;
            inst.objectId = entity;
            inst.isInstanced = render.isInstanced;

            if (reg.HasComponent<ChunkPropComponent>(entity)) {
                inst.chunkKey = reg.GetComponent<ChunkPropComponent>(entity).chunkKey;
            }
            else {
                inst.chunkKey = -1;
            }

            m_staticInstances.push_back(inst);
        }

        m_staticDirty = false;
        return m_staticInstances;
    }

    const std::vector<MeshInstance>& GetDynamicInstances() const {
        m_dynamicInstances.clear();
        Registry& reg = const_cast<Registry&>(m_registry);
        auto renderableGroup = reg.QueryGroup<RenderComponent, TransformComponent>();

        for (Entity entity : renderableGroup) {
            const auto& render = reg.GetComponent<RenderComponent>(entity);
            if (!render.isVisible || render.type == MeshType::Static) continue;

            auto& transform = reg.GetComponent<TransformComponent>(entity);
            if (transform.isDirty) {
                transform.UpdateMatrix(reg);
            }

            MeshInstance inst{};
            inst.meshHash = render.meshHash;
            inst.transform = transform.modelMatrix;
            inst.objectId = entity;

            m_dynamicInstances.push_back(inst);
        }
        return m_dynamicInstances;
    }

    void AddLight(const Light& light) { lights.push_back(light); hasModifiedLights = true; }
    void SetLights(const std::vector<Light>& newLights) {
        if (newLights.size() == lights.size() &&
            std::equal(newLights.begin(), newLights.end(), lights.begin(),
                [](const Light& a, const Light& b) {
                    return a.positionOrDir == b.positionOrDir && a.color == b.color &&
                        a.params == b.params;
                })) {
            return; // no change — don't force a dirty flag / renderer push
        }
        lights = newLights;
        hasModifiedLights = true;
    }
    bool HasModifiedLights() const { return hasModifiedLights; }
    void ClearModifiedLightsFlag() const { hasModifiedLights = false; }
    const std::vector<Light>& GetLights() const { return lights; }

    void Clear() {
        m_registry.Clear();
        lights.clear();
        m_staticInstances.clear();
        m_dynamicInstances.clear();
        m_staticDirty = true;
    }
};