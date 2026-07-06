#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct SubMesh {
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    int32_t  vertexOffset = 0;
    uint32_t textureId = 0;

    glm::vec3 boundingCenterLocal = glm::vec3(0.0f);
    float boundingRadiusLocal = 0.0f;
};

struct SceneObject {
    uint32_t firstSubMesh = 0;
    uint32_t subMeshCount = 0;
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    uint32_t objectId = 0;
};