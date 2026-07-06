#pragma once

#include "terrain.h"
#include "assetManager.h"
#include "scene.h"

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

    void InitializeWorld(VulkanRenderer& renderer, Scene& scene, int width, int depth) {
        Terrain terrain;
        terrain.GenerateTerrain(width, depth, seed); // Generate 100x100 grid

        std::vector<ModelVertex> modelVerts;
        modelVerts.reserve(terrain.vertices.size());
        for (const auto& v : terrain.vertices) {
            ModelVertex mv;
            mv.pos = v.pos;
            mv.normal = glm::vec3(0.0f, 1.0f, 0.0f); // default normal
            mv.texCoord = v.texCoord;
            modelVerts.push_back(mv);
        }

        // Register the terrain mesh
        //g_AssetManager.RegisterMesh("terrain", modelVerts, terrain.indices);

        // Add terrain instance to the scene (identity transform, objectId = 0)
        //scene.AddInstance("terrain", glm::mat4(1.0f), 0);
    }
};

