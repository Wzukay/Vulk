#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include "renderMesh.h"
#include "FastNoiseLite.h"

class Terrain
{
public:
	static void GenerateChunk(int chunkX, int chunkZ, int resolution, float chunkSize, int seed,
		std::vector<ModelVertex>& outVertices, std::vector<uint32_t>& outIndices);
};

namespace {
    // Classic smoothstep: turns a hard 0/1 cutoff into a soft transition,
    // used to blend regions in/out instead of a visible seam.
    float SmoothMask(float t, float edge0, float edge1) {
        float x = std::clamp((t - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    float Lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    // Bundles every noise layer used to build one height sample. Constructed
    // once per chunk (cheap) and passed around by reference so we don't
    // reconfigure FastNoiseLite per-vertex.
    struct TerrainNoiseLayers {
        FastNoiseLite warp;         // large-scale domain warp: breaks up the "grid" look
        FastNoiseLite warpDetail;   // second, finer warp layered on top for eroded-looking curves
        FastNoiseLite continent;    // broad rolling base elevation
        FastNoiseLite mask;         // decides WHERE hills/mountains are allowed to appear
        FastNoiseLite hills;        // medium bumps for the transition zone between plains and peaks
        FastNoiseLite mountainWarp; // independent warp applied only to the mountain layer
        FastNoiseLite mountain;     // ridged peaks, gated by mask
        FastNoiseLite detail;       // small-amplitude high-frequency bumps
        FastNoiseLite river;        // large-scale meandering line noise, carved into lowlands

        explicit TerrainNoiseLayers(int seed) {
            warp.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            warp.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
            warp.SetSeed(seed);
            warp.SetFrequency(0.0025f);
            warp.SetDomainWarpAmp(45.0f); // world units of coordinate distortion

            warpDetail.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            warpDetail.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
            warpDetail.SetSeed(seed + 500);
            warpDetail.SetFrequency(0.01f);   // finer than the primary warp
            warpDetail.SetDomainWarpAmp(12.0f); // smaller, adds organic wiggle on top

            continent.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            continent.SetFractalType(FastNoiseLite::FractalType_FBm);
            continent.SetFractalOctaves(5);
            continent.SetFrequency(0.006f);
            continent.SetSeed(seed + 1000);

            mask.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            mask.SetFractalType(FastNoiseLite::FractalType_FBm);
            mask.SetFractalOctaves(3);
            mask.SetFrequency(0.0015f); // very slow-changing: large terrain "regions"
            mask.SetSeed(seed + 2000);

            hills.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            hills.SetFractalType(FastNoiseLite::FractalType_FBm);
            hills.SetFractalOctaves(4);
            hills.SetFrequency(0.02f);
            hills.SetSeed(seed + 2500);

            mountainWarp.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            mountainWarp.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
            mountainWarp.SetSeed(seed + 2750);
            mountainWarp.SetFrequency(0.006f);
            mountainWarp.SetDomainWarpAmp(20.0f); // peaks no longer just mirror the continent's folds

            mountain.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            mountain.SetFractalType(FastNoiseLite::FractalType_Ridged);
            mountain.SetFractalOctaves(4);
            mountain.SetFractalLacunarity(1.9f);
            mountain.SetFractalGain(0.4f);
            mountain.SetFrequency(0.012f);
            mountain.SetSeed(seed + 3000);

            detail.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
            detail.SetFractalType(FastNoiseLite::FractalType_FBm);
            detail.SetFractalOctaves(3);
            detail.SetFrequency(0.09f);
            detail.SetSeed(seed + 4000);

            river.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            river.SetFractalType(FastNoiseLite::FractalType_FBm);
            river.SetFractalOctaves(2);
            river.SetFrequency(0.004f);
            river.SetSeed(seed + 5000);
        }
    };

    // Amplitude / shaping tuning — adjust these to taste.
    constexpr float kContinentAmplitude = 18.0f; // rolling base elevation scale
    constexpr float kContinentCurve = 1.6f;      // >1 flattens plains, sharpens highlands
    constexpr float kHillsAmplitude = 12.0f;     // medium bumps in the transition zone
    constexpr float kMountainAmplitude = 55.0f;  // peak height where mask allows it
    constexpr float kDetailAmplitude = 2.0f;     // small bumps/roughness everywhere
    constexpr float kTerraceStrength = 0.05f;    // 0 = smooth ridges, 1 = fully stepped
    constexpr float kRidgeSmoothRadius = 20.0f;  // world units; larger = softer/rounder peaks
    constexpr float kRidgeRoundness = 0.65f;     // <1 rounds off peak/slope contrast; 1 = no change

    // Three-tier mask: plains -> hills -> mountains, so the transition isn't
    // a single on/off switch.
    constexpr float kHillEdge0 = -0.10f;
    constexpr float kHillEdge1 = 0.25f;
    constexpr float kMountainEdge0 = 0.25f;
    constexpr float kMountainEdge1 = 0.60f;

    constexpr float kRiverDepth = 14.0f;  // how far rivers cut into the base terrain
    constexpr float kRiverWidth = 0.045f; // width of the carved channel, in noise units

    // Pushes noise values away from the middle so mid-range samples read as
    // flatter plains while extremes become more dramatic peaks/valleys —
    // a cheap stand-in for a proper spline/curve remap.
    float ShapeContinental(float c) {
        float s = (c < 0.0f) ? -1.0f : 1.0f;
        return s * std::pow(std::fabs(c), kContinentCurve);
    }

    float SampleRidgeSmoothed(FastNoiseLite& mountainNoise, float mx, float mz) {
        float center = mountainNoise.GetNoise(mx, mz);
        float n = mountainNoise.GetNoise(mx, mz + kRidgeSmoothRadius);
        float s = mountainNoise.GetNoise(mx, mz - kRidgeSmoothRadius);
        float e = mountainNoise.GetNoise(mx + kRidgeSmoothRadius, mz);
        float w = mountainNoise.GetNoise(mx - kRidgeSmoothRadius, mz);
        float blurred = center * 0.4f + (n + s + e + w) * 0.15f;
        return std::max(0.0f, blurred);
    }

    float SampleHeight(TerrainNoiseLayers& layers, float worldX, float worldZ) {
        // 1. Warp the sample coordinates twice, at different scales, so
        //    features curve organically instead of following the noise grid.
        float wx = worldX;
        float wz = worldZ;
        layers.warp.DomainWarp(wx, wz);
        layers.warpDetail.DomainWarp(wx, wz);

        // 2. Broad base elevation, reshaped for flatter plains / sharper highs.
        float continentalRaw = layers.continent.GetNoise(wx, wz);
        float continental = ShapeContinental(continentalRaw);

        // 3. Where are we: plains, hills, or mountains? (smooth 3-tier blend)
        float maskRaw = layers.mask.GetNoise(wx, wz);
        float hillFactor = SmoothMask(maskRaw, kHillEdge0, kHillEdge1);
        float mountainFactor = SmoothMask(maskRaw, kMountainEdge0, kMountainEdge1);

        // 4. Medium rolling hills for the transition zone.
        float hillsNoise = layers.hills.GetNoise(wx, wz);

        // 5. Ridged peaks sampled through their OWN independent warp, so
        //    mountain ranges don't just trace the same folds as the
        //    continent/hills — plus subtle terracing for rockier plateaus.
        float mx = wx, mz = wz;
        layers.mountainWarp.DomainWarp(mx, mz);
        float ridged = SampleRidgeSmoothed(layers.mountain, mx, mz);
        ridged = std::pow(ridged, kRidgeRoundness);
        if (kTerraceStrength > 0.0f) {
            float terraceSteps = 8.0f;
            float terraced = std::floor(ridged * terraceSteps) / terraceSteps;
            ridged = Lerp(ridged, terraced, kTerraceStrength);
        }

        // 6. Small-scale roughness so even plains aren't perfectly smooth.
        float detail = layers.detail.GetNoise(wx, wz);

        float height = continental * kContinentAmplitude
            + hillsNoise * hillFactor * kHillsAmplitude
            + ridged * mountainFactor * kMountainAmplitude
            + detail * kDetailAmplitude;

        // 7. Carve rivers through the lowlands: a thin band around the
        //    river noise's zero-crossing becomes a channel, faded out
        //    entirely once hills/mountains take over (no rivers on peaks).
        float riverRaw = layers.river.GetNoise(worldX, worldZ); // unwarped: keeps channels coherent/traceable
        float riverCarve = 1.0f - SmoothMask(std::fabs(riverRaw), 0.0f, kRiverWidth);
        riverCarve *= (1.0f - mountainFactor) * (1.0f - hillFactor * 0.6f);

        float riverBed = continental * kContinentAmplitude - kRiverDepth;
        height = Lerp(height, riverBed, riverCarve);

        return height;
    }
}

