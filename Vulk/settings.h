#pragma once

#include <string>

struct Settings {
    // --- Rendering ---
    float renderDistance = 1000.0f;          // far plane distance
    int maxMipLevels = 4;                    // maximum mip levels for textures (0 = auto)
    bool vsync = false;                       // enable V-Sync (FIFO present mode)
    bool anisotropicFiltering = true;        // enable anisotropic filtering
    float maxAnisotropy = 16.0f;             // max anisotropy level (if supported)
    int msaaSamples = 4;

    // --- Debug ---
    bool showStats = true;

    // --- Window ---
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;

    // --- Load/Save (optional) ---
    void LoadFromFile(const std::string& path = "settings.ini");
    void SaveToFile(const std::string& path = "settings.ini") const;
};

// Global instance (accessible from anywhere)
extern Settings g_Settings;