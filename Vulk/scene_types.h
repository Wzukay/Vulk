#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum class MeshType
{
    Static,
    Terrain
};

struct MeshInstance {
    std::string meshName;
    glm::mat4 transform;
    uint32_t objectId;

    MeshType type;

    int64_t chunkKey = -1;
};

struct SubMesh {
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    int32_t  vertexOffset = 0;
    uint32_t textureId = 0;
    uint32_t normalTextureId = 0;

    glm::vec3 boundingCenterLocal = glm::vec3(0.0f);
    float boundingRadiusLocal = 0.0f;
};

struct SceneObject {
    uint32_t firstSubMesh = 0;
    uint32_t subMeshCount = 0;

    glm::mat4 modelMatrix = glm::mat4(1.0f);
    uint32_t objectId = 0;

    MeshType type;
};

struct SceneLight {
    bool isPoint = false;          // false = directional, true = point
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f); // used if !isPoint
    glm::vec3 position = glm::vec3(0.0f);               // used if isPoint
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    float range = 10.0f;           // used if isPoint
};

static SceneLight MakeDirectional(const glm::vec3& direction, const glm::vec3& color, float intensity = 1.0f) {
    SceneLight l;
    l.isPoint = false;
    l.direction = glm::normalize(direction);
    l.color = color;
    l.intensity = intensity;
    return l;
}

static SceneLight MakePoint(const glm::vec3& position, const glm::vec3& color, float intensity = 1.0f, float range = 10.0f) {
    SceneLight l;
    l.isPoint = true;
    l.position = position;
    l.color = color;
    l.intensity = intensity;
    l.range = range;
    return l;
}