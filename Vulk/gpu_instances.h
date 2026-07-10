#pragma once

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

struct UniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 cameraPos;
    float ambient;
    float specularPower;
    uint32_t lightCount;
    float fogStart;
    float fogEnd;
    glm::vec2 screenSize; float _pad1[2];
    glm::mat4 inverseViewProj;
    glm::mat4 inverseProj;
    glm::mat4 inverseView;
};

struct PushConstants {
    glm::mat4 modelMatrix;      // 64 bytes
    uint32_t textureId;         // 4 bytes
    uint32_t normalTextureId;   // 4 bytes
    uint32_t objectId;          // 4 bytes
    float lodBlend;
};
struct TerrainChunkGPU {
    int64_t key = 0;
    glm::vec3 center{ 0.0f };
    float radius = 0.0f;
    int lod = 0;
    bool ready = false;

    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    float minHeight = 0.0f;
    float maxHeight = 0.0f;

    bool cachedOccluded = false;
};
struct ChunkSlot {
    uint32_t vertexOffset;
    uint32_t indexOffset;
    uint32_t indexCount;
    bool isAllocated;
    std::string objectName;
};

struct WaterPushConstants {
    glm::mat4 modelMatrix;      // usually identity; kept for flexibility (e.g. moving platforms)
    float time;
    uint32_t normalTextureId;
    float tiling;
    float waveStrength;
};
struct WaterBodyGPU {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
    uint32_t normalTextureId = 0;
    float tiling = 8.0f;
    float waveStrength = 0.15f;
};
struct StaleWaterBuffers {
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexMemory;
    uint32_t safeFrameIndex;
};

struct GrassInstance {
    glm::vec3 position;
    float rotation;
    glm::vec3 scale;
    float windOffset; // Variation pattern unique to this blade
};
struct GrassChunkGPU {
    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory instanceMemory = VK_NULL_HANDLE;
    uint32_t instanceCount = 0;

    glm::vec3 center = glm::vec3(0.0f);
    float radius = 0.0f;
};
struct GrassPushConstants {
    float time;
    uint32_t textureId;
    float windStrength;
    float windSpeed;
    float lodFactor;
};

struct FreeSpan { uint32_t offset; uint32_t count; };

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
    glm::vec3 worldCenter;
    float worldRadius;
    int64_t chunkKey = -1;
    float cachedMaxScale = 1.0f;
};
