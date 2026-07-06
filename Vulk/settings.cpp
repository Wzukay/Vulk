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
        else if (key == "maxMipLevels") maxMipLevels = std::stoi(value);
        else if (key == "vsync") vsync = (value == "true" || value == "1");
        else if (key == "anisotropicFiltering") anisotropicFiltering = (value == "true" || value == "1");
        else if (key == "maxAnisotropy") maxAnisotropy = std::stof(value);
        else if (key == "showTerrain") showTerrain = (value == "true" || value == "1");
        else if (key == "showModels") showModels = (value == "true" || value == "1");
        else if (key == "terrainScale") terrainScale = std::stof(value);
        else if (key == "showStats") showStats = (value == "true" || value == "1");
        else if (key == "wireframe") wireframe = (value == "true" || value == "1");
        else if (key == "frustumCulling") frustumCulling = (value == "true" || value == "1");
        else if (key == "windowWidth") windowWidth = std::stoi(value);
        else if (key == "windowHeight") windowHeight = std::stoi(value);
        else if (key == "fullscreen") fullscreen = (value == "true" || value == "1");
    }
}

void Settings::SaveToFile(const std::string& path) const {
    std::ofstream file(path);
    if (!file) return;
    file << "# Settings file\n";
    file << "renderDistance " << renderDistance << "\n";
    file << "maxMipLevels " << maxMipLevels << "\n";
    file << "vsync " << (vsync ? "true" : "false") << "\n";
    file << "anisotropicFiltering " << (anisotropicFiltering ? "true" : "false") << "\n";
    file << "maxAnisotropy " << maxAnisotropy << "\n";
    file << "showTerrain " << (showTerrain ? "true" : "false") << "\n";
    file << "showModels " << (showModels ? "true" : "false") << "\n";
    file << "terrainScale " << terrainScale << "\n";
    file << "showStats " << (showStats ? "true" : "false") << "\n";
    file << "wireframe " << (wireframe ? "true" : "false") << "\n";
    file << "frustumCulling " << (frustumCulling ? "true" : "false") << "\n";
    file << "windowWidth " << windowWidth << "\n";
    file << "windowHeight " << windowHeight << "\n";
    file << "fullscreen " << (fullscreen ? "true" : "false") << "\n";
}