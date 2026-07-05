#pragma once

#include "terrain.h"
#include "renderer.h"

#include <iostream>

class WorldGen {
private:
    int seed = 0;

public:
    void GetRandomSeed() {
        seed = std::rand() % 1000000; // Random seed between 0 and 999999
        std::cout << "Generated seed: " << seed << std::endl;
    }
    void SetSeed(int newSeed) { seed = newSeed; }
	int GetSeed() const { return seed; }

    void InitializeWorld(VulkanRenderer& renderer, int width, int depth) {
        Terrain terrain;
        terrain.GenerateTerrain(width, depth, seed); // Generate 100x100 grid

        // Send data to renderer
        renderer.UpdateGeometry(terrain.vertices, terrain.indices);
    }
};

