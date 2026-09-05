#pragma once

#include <string>

struct Settings {
    // --- Rendering ---
    float renderDistance = 5000.0f;
    float chunkSize = 512.0f;
    bool enableDRS = false;
    int targetFPS = 60;
    float renderScale = 0.75f;
    bool enableFSR = false;
    bool enableSSAO = true;
    int maxMipLevels = 4;
    bool vsync = false;
    int frameCap = 0;
    bool anisotropicFiltering = true;
    float maxAnisotropy = 16.0f;
    int msaaSamples = 4;
    int framesInFlight = 3;

    // --- World & LOD Distances (Normalized Ratios 0.0 to 1.0) ---
    float fogStartRatio = 0.70f;        // 70% of renderDistance
    float fogEndRatio = 1.0f;           // 100% of renderDistance

    float staticFadeStartRatio = 0.24f; // 24% of renderDistance
    float staticFadeEndRatio = 0.307f;  // ~30.7% of renderDistance

    float grassFadeStartRatio = 0.08f;  // 8% of renderDistance
    float grassFadeEndRatio = 0.12f;    // 12% of renderDistance

    float terrainLod0EndRatio = 0.30f;
    float terrainLod1EndRatio = 0.50f;
    float terrainLod2EndRatio = 0.72f;
    float terrainLod3EndRatio = 0.88f;

    // --- Debug ---
    bool showStats = true;
    float timeScale = 1.0f;

    // --- Window ---
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;

    void LoadFromFile(const std::string& path = "settings.ini");
    void SaveToFile(const std::string& path = "settings.ini") const;

    float GetFogStart() const { return renderDistance * fogStartRatio; }
    float GetFogEnd() const { return renderDistance * fogEndRatio; }
    float GetStaticFadeStart() const { return renderDistance * staticFadeStartRatio; }
    float GetStaticFadeEnd() const { return renderDistance * staticFadeEndRatio; }
    float GetGrassFadeStart() const { return renderDistance * grassFadeStartRatio; }
    float GetGrassFadeEnd() const { return renderDistance * grassFadeEndRatio; }

    float GetTerrainLod0End() const { return renderDistance * terrainLod0EndRatio; }
    float GetTerrainLod1End() const { return renderDistance * terrainLod1EndRatio; }
    float GetTerrainLod2End() const { return renderDistance * terrainLod2EndRatio; }
    float GetTerrainLod3End() const { return renderDistance * terrainLod3EndRatio; }
};

// Global instance (accessible from anywhere)
extern Settings g_Settings;