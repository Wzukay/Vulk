#pragma once

#include "light.h"
#include <glm/glm.hpp>
#include <optional>

struct DayNightManager {
    float timeOfDay = 12.0f;
    float timeScale = g_Settings.timeScale / 10.0f;
    float coverage = 0.45;
    float cloudTime = 0.0f;

    Light sunLight;
    std::optional<Light> moonLight;

    // NEW: Expose sky data to the engine
    glm::vec3 zenithColor;
    glm::vec3 horizonColor;
    float starFade;

    void Tick(float deltaTime) {
        timeOfDay += deltaTime * timeScale;
        if (timeOfDay >= 24.0f) timeOfDay -= 24.0f;

        cloudTime += deltaTime * timeScale;

        float timeFraction = timeOfDay / 24.0f;
        float angle = (timeFraction * 6.283185f) - 1.570796f;
        glm::vec3 sunDir(0.0f, std::sin(angle), std::cos(angle));

        float t = sunDir.y;
        float b1 = glm::smoothstep(-0.1f, 0.0f, t);
        float b2 = glm::smoothstep(0.0f, 0.1f, t);
        float b3 = glm::smoothstep(0.1f, 0.35f, t);

        // --- Sun Colors ---
        glm::vec3 twilightCol = glm::vec3(0.8f, 0.3f, 0.6f);
        glm::vec3 sunriseCol = glm::vec3(1.0f, 0.6f, 0.2f);
        glm::vec3 dayCol = glm::vec3(1.0f, 0.95f, 0.85f);
        glm::vec3 sunColor = glm::mix(glm::mix(glm::mix(glm::vec3(0.0f), twilightCol, b1), sunriseCol, b2), dayCol, b3);
        float sunIntensity = glm::smoothstep(-0.05f, 0.3f, t) * 1.1f;
        sunLight = Light::Directional(sunDir, sunColor, sunIntensity);

        // --- Moon Colors ---
        float moonBlend = glm::smoothstep(0.0f, -0.2f, t);
        if (moonBlend > 0.0f) {
            moonLight = Light::Directional(sunDir * -1.0f, glm::vec3(0.55f, 0.6f, 0.65f), moonBlend * 0.05f);
        }
        else {
            moonLight.reset();
        }

        // --- NEW: Sky Colors ---
        glm::vec3 zNight = glm::vec3(0.01f, 0.015f, 0.04f);
        glm::vec3 zTwilight = glm::vec3(0.15f, 0.1f, 0.2f);
        glm::vec3 zSunrise = glm::vec3(0.2f, 0.3f, 0.5f);
        glm::vec3 zDay = glm::vec3(0.08f, 0.25f, 0.6f);
        zenithColor = glm::mix(glm::mix(glm::mix(zNight, zTwilight, b1), zSunrise, b2), zDay, b3);

        glm::vec3 hNight = glm::vec3(0.02f, 0.02f, 0.03f);
        glm::vec3 hTwilight = glm::vec3(0.7f, 0.2f, 0.25f);
        glm::vec3 hSunrise = glm::vec3(0.8f, 0.5f, 0.3f);
        glm::vec3 hDay = glm::vec3(0.5f, 0.7f, 0.9f);
        horizonColor = glm::mix(glm::mix(glm::mix(hNight, hTwilight, b1), hSunrise, b2), hDay, b3);

        starFade = 1.0f - b1; // 1.0 = Midnight, 0.0 = Daylight
    }
};