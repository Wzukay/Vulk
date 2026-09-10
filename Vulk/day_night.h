#pragma once

#include "light.h"
#include <glm/glm.hpp>
#include <optional>

struct DayNightManager {
    float timeOfDay = 12.0f;
    float timeScale = g_Settings.timeScale / 10.0f;

    Light sunLight;
    std::optional<Light> moonLight;

    void Tick(float deltaTime) {
        timeOfDay += deltaTime * timeScale;
        if (timeOfDay >= 24.0f) timeOfDay -= 24.0f;

        float timeFraction = timeOfDay / 24.0f;
        float angle = (timeFraction * 6.283185f) - 1.570796f;

        glm::vec3 sunDir(0.0f, std::sin(angle), std::cos(angle));

        // 4-Phase Blends
        float t = sunDir.y;
        float b1 = glm::smoothstep(-0.1f, 0.0f, t);  // Night -> Twilight
        float b2 = glm::smoothstep(0.0f, 0.1f, t);   // Twilight -> Sunrise
        float b3 = glm::smoothstep(0.1f, 0.35f, t);  // Sunrise -> Day

        // Stylized Light Colors
        glm::vec3 twilightCol = glm::vec3(0.8f, 0.3f, 0.6f);
        glm::vec3 sunriseCol = glm::vec3(1.0f, 0.6f, 0.2f);
        glm::vec3 dayCol = glm::vec3(1.0f, 0.95f, 0.85f);

        // Mix the sun color
        glm::vec3 sunColor = glm::mix(glm::mix(glm::mix(glm::vec3(0.0f), twilightCol, b1), sunriseCol, b2), dayCol, b3);
        float sunIntensity = glm::smoothstep(-0.05f, 0.3f, t) * 1.1f;

        sunLight = Light::Directional(sunDir, sunColor, sunIntensity);

        // Stylized Cyan Moonlight
        float moonBlend = glm::smoothstep(0.0f, -0.2f, t);
        if (moonBlend > 0.0f) {
            glm::vec3 moonDir = sunDir * -1.0f;
            glm::vec3 moonColor = glm::vec3(0.55f, 0.6f, 0.65f);
            moonLight = Light::Directional(moonDir, moonColor, moonBlend * 0.05f);
        }
        else {
            moonLight.reset();
        }
    }
};