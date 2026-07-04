#pragma once

#include <vector>
#include "FastNoiseLite.h"
#include "vertex.h"

class Terrain
{
public:
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	void GenerateTerrain(int width, int depth, int seed);
};

