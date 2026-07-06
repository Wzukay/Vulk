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
public:
    void AddInstance(const std::string& meshName, const glm::mat4& transform, uint32_t objectId) {
        instances.push_back({ meshName, transform, objectId });
    }
    const std::vector<MeshInstance>& GetInstances() const { return instances; }
    void Clear() { instances.clear(); }

private:
    std::vector<MeshInstance> instances;
};