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
layout(location = 3) out float outBladeRand;

const vec3 VERT_POSITIONS[12] = vec3[12](
    // Front‑back quad (6 vertices)
    vec3(-0.5, 0.0, 0.0), vec3( 0.5, 0.0, 0.0), vec3( 0.5, 1.0, 0.0),
    vec3(-0.5, 0.0, 0.0), vec3( 0.5, 1.0, 0.0), vec3(-0.5, 1.0, 0.0),
    // Left‑right quad (6 vertices)
    vec3(0.0, 0.0, -0.5), vec3(0.0, 0.0,  0.5), vec3(0.0, 1.0,  0.5),
    vec3(0.0, 0.0, -0.5), vec3(0.0, 1.0,  0.5), vec3(0.0, 1.0, -0.5)
);

const vec2 VERT_UVS[12] = vec2[12](
    // Front-back quad (Corrected Vulkan UVs)
    vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0),
    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0),
    // Left-right quad (Corrected Vulkan UVs)
    vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0),
    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0)
);

void main() {
    vec3 localPos = VERT_POSITIONS[gl_VertexIndex];
    vec2 uv = VERT_UVS[gl_VertexIndex];
    outUV = uv;

    float heightNorm = localPos.y; // 0 at base, 1 at tip

    // ---- Per‑blade random (passed to fragment) ----
    float bladeRand = fract(sin(inInstancePos.x * 12.9898 + inInstancePos.z * 78.233 + inInstancePos.y * 45.164) * 43758.5453);
    outBladeRand = bladeRand;

    vec3 scaledPos = localPos * inInstanceScale;

    // ---- Static curvature with per‑blade variation ----
    float bladeHeight = inInstanceScale.y;
    float bendAmount = 0.10 + bladeRand * 0.15;
    float bendOffset = bendAmount * bladeHeight * heightNorm * heightNorm;
    scaledPos.x += bendOffset;

    // ---- Rotation around Y ----
    float c = cos(inInstanceRotation);
    float s = sin(inInstanceRotation);
    vec3 rotatedPos = vec3(
        scaledPos.x * c - scaledPos.z * s,
        scaledPos.y,
        scaledPos.x * s + scaledPos.z * c
    );

    // ---- Spherical normal (based on original, un‑tapered localPos) ----
    vec3 sphereNormal = normalize(vec3(localPos.x, 0.35, localPos.z));
    outNormal = normalize(vec3(sphereNormal.x * c - sphereNormal.z * s,
                               sphereNormal.y,
                               sphereNormal.x * s + sphereNormal.z * c));

    // ---- Wind ----
    float bendFactor = heightNorm * heightNorm;
    float adjustedTime = push.time * push.windSpeed + inInstanceWindOffset;

    float baseWave = sin(adjustedTime + inInstancePos.x * 0.15 + inInstancePos.z * 0.15);
    float microFlutter = sin(adjustedTime * 2.5 + inInstancePos.y) * cos(adjustedTime * 1.8);
    float totalWave = (baseWave * 1.2 + microFlutter * 0.3);

    // ---- Gust envelope ----
    float gustTime = push.time * 0.15 + inInstancePos.x * 0.005 + inInstancePos.z * 0.007;
    float gust = sin(gustTime) * 0.5 + 0.5;
    gust = gust * 0.4 + 0.8;
    float finalWindStrength = push.windStrength * gust;

    float windMultiplier = 0.7 + 0.6 * bladeRand;
    float windScale = 1.0 - push.lodFactor;

    rotatedPos.x += totalWave * finalWindStrength * windScale * bendFactor * windMultiplier * 0.5;
    rotatedPos.z += totalWave * finalWindStrength * 0.2 * windScale * bendFactor * windMultiplier;
    rotatedPos.y -= (totalWave * totalWave) * finalWindStrength * 0.05 * windScale * bendFactor * windMultiplier;

    outWorldPos = rotatedPos + inInstancePos;
    gl_Position = ubo.proj * ubo.view * vec4(outWorldPos, 1.0);
}