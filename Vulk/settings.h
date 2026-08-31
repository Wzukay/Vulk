#pragma once

#include <string>

struct Settings {
    // --- Rendering ---
    float renderDistance = 5000.0f;
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

    // --- World & LOD Distances (The Source of Truth) ---
    float fogStart = 3500.0f;
    float fogEnd = 5000.0f;

    float staticFadeStart = 1200.0f;
    float staticFadeEnd = 1536.0f;     // Should match where Chunk LOD 3 culls

    float grassFadeStart = 400.0f;
    float grassFadeEnd = 600.0f;     // Grass usually culls much earlier than trees

    // --- Debug ---
    bool showStats = true;
    float timeScale = 1.0f;

    // --- Window ---
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;

    void LoadFromFile(const std::string& path = "settings.ini");
    void SaveToFile(const std::string& path = "settings.ini") const;
};

// Global instance (accessible from anywhere)
extern Settings g_Settings;