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
        else if (key == "renderScale") renderScale = std::stof(value);
        else if (key == "enableFSR") enableFSR = (value == "true" || value == "1");
        else if (key == "enableSSAO") enableSSAO = (value == "true" || value == "1");
        else if (key == "fogStart") fogStart = std::stof(value);
        else if (key == "fogEnd") fogEnd = std::stof(value);
        else if (key == "treeFadeStart") staticFadeStart = std::stof(value);
        else if (key == "treeFadeEnd") staticFadeEnd = std::stof(value);
        else if (key == "grassFadeStart") grassFadeStart = std::stof(value);
        else if (key == "grassFadeEnd") grassFadeEnd = std::stof(value);
        else if (key == "maxMipLevels") maxMipLevels = std::stoi(value);
        else if (key == "vsync") vsync = (value == "true" || value == "1");
        else if (key == "anisotropicFiltering") anisotropicFiltering = (value == "true" || value == "1");
        else if (key == "maxAnisotropy") maxAnisotropy = std::stof(value);
		else if (key == "msaaSamples") msaaSamples = std::stoi(value);
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
    file << "# Settings file\n";
    file << "renderDistance " << renderDistance << "\n";
    file << "renderScale " << renderScale << "\n";
    file << "enableFSR " << (enableFSR ? "true" : "false") << "\n";
    file << "enableSSAO " << (enableSSAO ? "true" : "false") << "\n";
    file << "fogStart " << fogStart << "\n";
    file << "fogEnd " << fogEnd << "\n";
    file << "staticFadeStart " << staticFadeStart << "\n";
    file << "staticFadeEnd " << staticFadeEnd << "\n";
    file << "grassFadeStart " << grassFadeStart << "\n";
    file << "grassFadeEnd " << grassFadeEnd << "\n";
    file << "maxMipLevels " << maxMipLevels << "\n";
    file << "vsync " << (vsync ? "true" : "false") << "\n";
    file << "anisotropicFiltering " << (anisotropicFiltering ? "true" : "false") << "\n";
    file << "maxAnisotropy " << maxAnisotropy << "\n";
	file << "msaaSamples " << msaaSamples << "\n";
    file << "showStats " << (showStats ? "true" : "false") << "\n";
    file << "timeScale " << timeScale << "\n";
    file << "windowWidth " << windowWidth << "\n";
    file << "windowHeight " << windowHeight << "\n";
    file << "fullscreen " << (fullscreen ? "true" : "false") << "\n";
}