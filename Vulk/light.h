#pragma once

#include <glm/glm.hpp>

// A single, unified, GPU-aligned light structure
struct Light {
    alignas(16) glm::vec4 positionOrDir; // xyz = pos/dir, w: 0 = directional, 1 = point
    alignas(16) glm::vec4 color;         // rgb = color, a = intensity
    alignas(16) glm::vec4 params;        // x = range (point lights), yzw = unused padding

    // Helper to easily create a Directional Light (replaces SceneLight::MakeDirectional)
    static Light Directional(const glm::vec3& direction, const glm::vec3& color, float intensity = 1.0f) {
        Light l{};
        l.positionOrDir = glm::vec4(glm::normalize(direction), 0.0f);
        l.color = glm::vec4(color, intensity);
        l.params = glm::vec4(0.0f);
        return l;
    }

    // Helper to easily create a Point Light (replaces SceneLight::MakePoint)
    static Light Point(const glm::vec3& position, const glm::vec3& color, float intensity = 1.0f, float range = 10.0f) {
        Light l{};
        l.positionOrDir = glm::vec4(position, 1.0f);
        l.color = glm::vec4(color, intensity);
        l.params = glm::vec4(range, 0.0f, 0.0f, 0.0f);
        return l;
    }
};