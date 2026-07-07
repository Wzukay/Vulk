#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <algorithm>

#include "scene_types.h"

class Scene {
private:
    std::vector<MeshInstance> instances;
    std::vector<SceneLight> lights;

    mutable bool hasModifiedLights = false;

public:
    void AddInstance(
        const std::string& meshName,
        const glm::mat4& transform,
        uint32_t objectId)
    {
        instances.push_back(
            {
                meshName,
                transform,
                objectId,
                MeshType::Static
            });
    }

    void RemoveChunk(int64_t key)
    {
        instances.erase(
            std::remove_if(
                instances.begin(),
                instances.end(),
                [&](const MeshInstance& inst)
                {
                    return inst.chunkKey == key;
                }),
            instances.end());
    }

    void RemoveInstance(const std::string& meshName) { // NEW
        instances.erase(
            std::remove_if(instances.begin(), instances.end(),
                [&](const MeshInstance& inst) { return inst.meshName == meshName; }),
            instances.end());
    }

    void AddLight(const SceneLight& light) { lights.push_back(light); hasModifiedLights = true; }
    bool HasModifiedLights() const { return hasModifiedLights; }
    void ClearModifiedLightsFlag() const { hasModifiedLights = false; }
    const std::vector<SceneLight>& GetLights() const { return lights; }

    const std::vector<MeshInstance>& GetInstances() const { return instances; }
    void Clear() { instances.clear(); lights.clear(); }
};