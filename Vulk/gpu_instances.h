#pragma once

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

struct UniformBufferObject {
    alignas(16) glm::mat4 view;              // 64 bytes
    alignas(16) glm::mat4 proj;              // 64 bytes

    // Chunk 1: 16 bytes
    alignas(16) glm::vec3 cameraPos;         // 12 bytes
    float ambient;                           // 4 bytes

    // Chunk 2: 16 bytes
    alignas(16) glm::vec4 fadeParams;        // 16 bytes

    // Chunk 3: 16 bytes
    alignas(8)  glm::vec2 screenSize;        // 8 bytes
    float specularPower;                     // 4 bytes
    uint32_t lightCount;                     // 4 bytes

    // Chunk 4: 16 bytes (Explicitly padded to prevent C++ from crushing the struct)
    float fogStart;                          // 4 bytes
    float fogEnd;                            // 4 bytes
    float _pad2[2];                          // 8 bytes of padding

    // Matrices (64 bytes each, alignas(16) guarantees std140 matrix boundaries)
    alignas(16) glm::mat4 inverseViewProj;
    alignas(16) glm::mat4 inverseProj;
    alignas(16) glm::mat4 inverseView;
};

struct Light {
    alignas(16) glm::vec4 positionOrDir; // w: 0 = directional, 1 = point
    alignas(16) glm::vec4 color;         // rgb = color, a = intensity
    alignas(16) glm::vec4 params;        // x = range (point lights)

    static Light Directional(const glm::vec3& direction, const glm::vec3& color, float intensity = 1.0f) {
        Light l{};
        l.positionOrDir = glm::vec4(glm::normalize(direction), 0.0f);
        l.color = glm::vec4(color, intensity);
        l.params = glm::vec4(0.0f);
        return l;
    }

    static Light Point(const glm::vec3& position, const glm::vec3& color, float intensity = 1.0f, float range = 10.0f) {
        Light l{};
        l.positionOrDir = glm::vec4(position, 1.0f);
        l.color = glm::vec4(color, intensity);
        l.params = glm::vec4(range, 0.0f, 0.0f, 0.0f);
        return l;
    }
};

struct DrawEntry {
    uint32_t objectIndex;
    uint32_t subMeshIndex; // index into globalSubMeshes
    float distSq;
    int64_t chunkKey = -1;
    float cachedMaxScale = 1.0f;
};

struct FrustumPlane {
    glm::vec3 normal;
    float distance;
};

struct GrassChunkMetadata {
    glm::vec3 center;
    float radius;
    uint32_t firstInstance;
    uint32_t instanceCount;
    uint32_t pad[2]; // align to 16 bytes
};

struct IndirectCommand {
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t firstVertex;
    uint32_t firstInstance;
};