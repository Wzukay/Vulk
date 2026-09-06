#include "Settings.h"
#include <fstream>
#include <iostream>
#include <sstream>

Settings g_Settings;  // define global instance

void Settings::LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string key, value;
        if (!(iss >> key >> value)) continue;

        if (key == "renderDistance") renderDistance = std::stof(value);
        else if (key == "chunkSize") chunkSize = std::stof(value);
        else if (key == "chunkResolution") chunkResolution = std::stof(value);
        else if (key == "enableDRS") enableDRS = (value == "true" || value == "1");
        else if (key == "targetFPS") targetFPS = std::stof(value);
        else if (key == "renderScale") renderScale = std::stof(value);
        else if (key == "enableFSR") enableFSR = (value == "true" || value == "1");
        else if (key == "enableSSAO") enableSSAO = (value == "true" || value == "1");
        else if (key == "fogStartRatio") fogStartRatio = std::stof(value);
        else if (key == "fogEndRatio") fogEndRatio = std::stof(value);
        else if (key == "staticFadeStartRatio") staticFadeStartRatio = std::stof(value);
        else if (key == "staticFadeEndRatio") staticFadeEndRatio = std::stof(value);
        else if (key == "grassFadeStartRatio") grassFadeStartRatio = std::stof(value);
        else if (key == "grassFadeEndRatio") grassFadeEndRatio = std::stof(value);
        else if (key == "terrainLod0EndRatio") terrainLod0EndRatio = std::stof(value);
        else if (key == "terrainLod1EndRatio") terrainLod1EndRatio = std::stof(value);
        else if (key == "terrainLod2EndRatio") terrainLod2EndRatio = std::stof(value);
        else if (key == "terrainLod3EndRatio") terrainLod3EndRatio = std::stof(value);
        else if (key == "maxMipLevels") maxMipLevels = std::stoi(value);
        else if (key == "vsync") vsync = (value == "true" || value == "1");
        else if (key == "frameCap") frameCap = std::stoi(value);
        else if (key == "anisotropicFiltering") anisotropicFiltering = (value == "true" || value == "1");
        else if (key == "maxAnisotropy") maxAnisotropy = std::stof(value);
        else if (key == "antiAliasingMode") antiAliasingMode = std::stoi(value);
        else if (key == "msaaSamples") msaaSamples = std::stoi(value);
        else if (key == "framesInFlight") framesInFlight = std::stoi(value);
        else if (key == "showStats") showStats = (value == "true" || value == "1");
        else if (key == "timeScale") timeScale = std::stof(value);
        else if (key == "windowWidth") windowWidth = std::stoi(value);
        else if (key == "windowHeight") windowHeight = std::stoi(value);
        else if (key == "fullscreen") fullscreen = (value == "true" || value == "1");
    }
}

void Settings::SaveToFile(const std::string& path) const {
    std::ofstream file(path);
    if (!file) return;

    file << "# Settings file\n\n";

    file << "# --- Rendering ---\n";
    file << "renderDistance " << renderDistance << "\n";
    file << "chunkSize " << chunkSize << "\n";
    file << "chunkResolution " << chunkResolution << "\n";
    file << "enableDRS " << (enableDRS ? "true" : "false") << "\n";
    file << "targetFPS " << targetFPS << "\n";
    file << "renderScale " << renderScale << "\n";
    file << "enableFSR " << (enableFSR ? "true" : "false") << "\n";
    file << "enableSSAO " << (enableSSAO ? "true" : "false") << "\n";
    file << "maxMipLevels " << maxMipLevels << "\n";
    file << "vsync " << (vsync ? "true" : "false") << "\n";
    file << "frameCap " << frameCap << "\n";
    file << "anisotropicFiltering " << (anisotropicFiltering ? "true" : "false") << "\n";
    file << "maxAnisotropy " << maxAnisotropy << "\n";
    file << "antiAliasingMode " << antiAliasingMode << "\n";
    file << "msaaSamples " << msaaSamples << "\n";
    file << "framesInFlight " << framesInFlight << "\n\n";

    file << "# --- World & LOD Distances ---\n";
    file << "fogStartRatio " << fogStartRatio << "\n";
    file << "fogEndRatio " << fogEndRatio << "\n";
    file << "staticFadeStartRatio " << staticFadeStartRatio << "\n";
    file << "staticFadeEndRatio " << staticFadeEndRatio << "\n";
    file << "grassFadeStartRatio " << grassFadeStartRatio << "\n";
    file << "grassFadeEndRatio " << grassFadeEndRatio << "\n";
    file << "terrainLod0EndRatio " << terrainLod0EndRatio << "\n";
    file << "terrainLod1EndRatio " << terrainLod1EndRatio << "\n";
    file << "terrainLod2EndRatio " << terrainLod2EndRatio << "\n";
    file << "terrainLod3EndRatio " << terrainLod3EndRatio << "\n\n";

    file << "# --- Debug ---\n";
    file << "showStats " << (showStats ? "true" : "false") << "\n";
    file << "timeScale " << timeScale << "\n\n";

    file << "# --- Window ---\n";
    file << "windowWidth " << windowWidth << "\n";
    file << "windowHeight " << windowHeight << "\n";
    file << "fullscreen " << (fullscreen ? "true" : "false") << "\n";
}