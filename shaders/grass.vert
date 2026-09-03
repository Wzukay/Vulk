#version 450
#include "common_structures.glsl"

layout(location = 0) in vec3 inInstancePos;
layout(location = 1) in float inInstanceRotation;
layout(location = 2) in vec3 inInstanceScale;
layout(location = 3) in float inInstanceWindOffset;

layout(push_constant) uniform PushConstants {
    float time;
    uint textureId;
    float windStrength;
    float windSpeed;
    float lodFactor;
} push;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) out vec3 outNormal;

// 1. Predefined Positions (9 vertices)
const vec3 VERT_POSITIONS[9] = vec3[9](
    // Bottom segment
    vec3(-0.55, 0.0, 0.0), vec3( 0.55, 0.0, 0.0), vec3( 0.35, 0.5, 0.0),
    vec3(-0.55, 0.0, 0.0), vec3( 0.35, 0.5, 0.0), vec3(-0.35, 0.5, 0.0),
    // Top segment
    vec3(-0.35, 0.5, 0.0), vec3( 0.35, 0.5, 0.0), vec3( 0.0,  1.0, 0.0)
);

const vec2 VERT_UVS[9] = vec2[9](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.8, 0.5),
    vec2(0.0, 0.0), vec2(0.8, 0.5), vec2(0.2, 0.5),
    vec2(0.2, 0.5), vec2(0.8, 0.5), vec2(0.5, 1.0)
);

// 2. Pre-normalized Spherical Base Normals (Zero runtime sqrt/normalize cost)
const vec3 VERT_NORMALS[9] = vec3[9](
    vec3(-0.84366, 0.53688, 0.0), vec3( 0.84366, 0.53688, 0.0), vec3( 0.70711, 0.70711, 0.0),
    vec3(-0.84366, 0.53688, 0.0), vec3( 0.70711, 0.70711, 0.0), vec3(-0.70711, 0.70711, 0.0),
    vec3(-0.70711, 0.70711, 0.0), vec3( 0.70711, 0.70711, 0.0), vec3( 0.0,     1.0,     0.0)
);

void main() {
    uint vIdx = gl_VertexIndex;
    vec3 localPos = VERT_POSITIONS[vIdx];
    outUV = VERT_UVS[vIdx];

    float heightNorm = localPos.y; // 0 at base, 0.5 mid, 1 at tip
    float bendFactor = heightNorm * heightNorm;

    // Per-blade pseudorandom hash
    float bladeRand = fract(sin(inInstancePos.x * 12.9898 + inInstancePos.z * 78.233 + inInstancePos.y * 45.164) * 43758.5453);

    vec3 scaledPos = localPos * inInstanceScale;

    // Static curvature
    float bendOffset = (0.10 + bladeRand * 0.15) * inInstanceScale.y * bendFactor;
    scaledPos.x += bendOffset;

    // Rotation around Y
    float c = cos(inInstanceRotation);
    float s = sin(inInstanceRotation);

    vec3 rotatedPos = vec3(
        scaledPos.x * c - scaledPos.z * s,
        scaledPos.y,
        scaledPos.x * s + scaledPos.z * c
    );

    // Rotated Normal using precomputed table lookup
    vec3 sphereNormal = VERT_NORMALS[vIdx];
    outNormal = vec3(
        sphereNormal.x * c - sphereNormal.z * s,
        sphereNormal.y,
        sphereNormal.x * s + sphereNormal.z * c
    );

    // Wind Animation (Skip entirely if heightNorm == 0 or beyond distance range)
    float windScale = 1.0 - push.lodFactor;

    if (windScale > 0.1 && heightNorm > 0.0) {
        vec3 camDiff = ubo.cameraPos - inInstancePos;
        float distSq = dot(camDiff, camDiff);

        const float ANIM_MAX_DIST_SQ = 900.0;   // 30.0 * 30.0
        const float ANIM_FADE_START_SQ = 100.0; // 10.0 * 10.0
        const float INV_RANGE_SQ = 1.0 / (ANIM_MAX_DIST_SQ - ANIM_FADE_START_SQ);

        if (distSq < ANIM_MAX_DIST_SQ) {
            float animFactor = 1.0 - clamp((distSq - ANIM_FADE_START_SQ) * INV_RANGE_SQ, 0.0, 1.0);
            float windMultiplier = 0.5 + 0.4 * bladeRand;

            float adjustedTime = push.time * push.windSpeed + inInstanceWindOffset;

            float baseWave = sin(adjustedTime + inInstancePos.x * 0.15 + inInstancePos.z * 0.15);
            float microFlutter = sin(adjustedTime * 2.5 + inInstancePos.y) * cos(adjustedTime * 1.8);
            float totalWave = (baseWave * 1.2 + microFlutter * 0.3);

            float gustTime = push.time * 0.15 + inInstancePos.x * 0.005 + inInstancePos.z * 0.007;
            float gust = (sin(gustTime) * 0.5 + 0.5) * 0.4 + 0.8;
            float finalWindStrength = push.windStrength * gust;

            float effectiveWind = finalWindStrength * animFactor * bendFactor * windMultiplier;

            rotatedPos.x += totalWave * effectiveWind * 0.5;
            rotatedPos.z += totalWave * effectiveWind * 0.2;
            rotatedPos.y -= (totalWave * totalWave) * effectiveWind * 0.05;
        }
    }

    outWorldPos = rotatedPos + inInstancePos;
    gl_Position = ubo.proj * ubo.view * vec4(outWorldPos, 1.0);
}