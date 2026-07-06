#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>

#include "scene_types.h"

struct MeshInstance {
    std::string meshName;
    glm::mat4 transform;
    uint32_t objectId;
};

class Scene {
private:
    std::vector<MeshInstance> instances;
    std::vector<SceneLight> lights;

    mutable bool hasModifiedLights = false;

public:
    void AddInstance(const std::string& meshName, const glm::mat4& transform, uint32_t objectId) {
        instances.push_back({ meshName, transform, objectId });
    }

    void AddLight(const SceneLight& light) {
        lights.push_back(light);
        hasModifiedLights = true;
    }
    bool HasModifiedLights() const { return hasModifiedLights; }
    void ClearModifiedLightsFlag() const { hasModifiedLights = false; }
    const std::vector<SceneLight>& GetLights() const { return lights; }

    const std::vector<MeshInstance>& GetInstances() const { return instances; }
    void Clear() { instances.clear(); lights.clear(); }
};