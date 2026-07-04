#include "terrain.h"

void Terrain::GenerateTerrain(int width, int depth, int seed = 123456) {
    vertices.clear();
    indices.clear();

    FastNoiseLite noise;
    noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    noise.SetFrequency(0.05f);

    noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    noise.SetFractalOctaves(3);

    noise.SetSeed(seed);

    // 1. Generate Vertices
    for (int y = 0; y < depth; y++) {
        for (int x = 0; x < width; x++) {
            float h = noise.GetNoise((float)x, (float)y) * 10.0f;
            
            Vertex v;
            v.pos = glm::vec3((float)x, (float)y, h);
            v.color = glm::vec3(h * 0.5f + 0.5f, 0.5f, 1.0f);
            vertices.push_back(v);
        }
    }

    // 2. Generate Indices (Triangulation)
    for (int y = 0; y < depth - 1; y++) {
        for (int x = 0; x < width - 1; x++) {
            uint32_t i0 = y * width + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = (y + 1) * width + x;
            uint32_t i3 = i2 + 1;

            // Two triangles per grid square
            indices.insert(indices.end(), { i0, i2, i1, i1, i2, i3 });
        }
    }
}