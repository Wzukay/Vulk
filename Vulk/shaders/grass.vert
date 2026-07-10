#version 450

layout(location = 0) in vec3 inInstancePos;
layout(location = 1) in float inInstanceRotation;
layout(location = 2) in vec3 inInstanceScale;
layout(location = 3) in float inInstanceWindOffset;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    layout(offset = 128) vec3 cameraPos;
    layout(offset = 140) float ambient;
    layout(offset = 144) float specularPower;
    layout(offset = 148) uint lightCount;
    layout(offset = 152) float fogStart;
    layout(offset = 156) float fogEnd;
    layout(offset = 160) vec2 screenSize;
    layout(offset = 176) mat4 inverseViewProj;
    layout(offset = 240) mat4 inverseProj;
    layout(offset = 304) mat4 inverseView;
} ubo;

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

// ---- 24‑vertex subdivided blade (2 segments) ----
const vec3 VERT_POSITIONS[24] = vec3[](
    vec3(-0.5, 0.0, 0.0), vec3( 0.5, 0.0, 0.0), vec3(-0.5, 0.5, 0.0),
    vec3( 0.5, 0.0, 0.0), vec3( 0.5, 0.5, 0.0), vec3(-0.5, 0.5, 0.0),
    vec3(-0.5, 0.5, 0.0), vec3( 0.5, 0.5, 0.0), vec3(-0.5, 1.0, 0.0),
    vec3( 0.5, 0.5, 0.0), vec3( 0.5, 1.0, 0.0), vec3(-0.5, 1.0, 0.0),
    vec3(0.0, 0.0, -0.5), vec3(0.0, 0.0,  0.5), vec3(0.0, 0.5, -0.5),
    vec3(0.0, 0.0,  0.5), vec3(0.0, 0.5,  0.5), vec3(0.0, 0.5, -0.5),
    vec3(0.0, 0.5, -0.5), vec3(0.0, 0.5,  0.5), vec3(0.0, 1.0, -0.5),
    vec3(0.0, 0.5,  0.5), vec3(0.0, 1.0,  0.5), vec3(0.0, 1.0, -0.5)
);

const vec2 VERT_UVS[24] = vec2[](
    vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, 0.5),
    vec2(1.0, 1.0), vec2(1.0, 0.5), vec2(0.0, 0.5),
    vec2(0.0, 0.5), vec2(1.0, 0.5), vec2(0.0, 0.0),
    vec2(1.0, 0.5), vec2(1.0, 0.0), vec2(0.0, 0.0),
    vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, 0.5),
    vec2(1.0, 1.0), vec2(1.0, 0.5), vec2(0.0, 0.5),
    vec2(0.0, 0.5), vec2(1.0, 0.5), vec2(0.0, 0.0),
    vec2(1.0, 0.5), vec2(1.0, 0.0), vec2(0.0, 0.0)
);

void main() {
    vec3 localPos = VERT_POSITIONS[gl_VertexIndex];
    vec2 uv = VERT_UVS[gl_VertexIndex];
    outUV = uv;

    float heightNorm = localPos.y; // 0 at base, 1 at tip

    // ---- Per‑blade random (passed to fragment) ----
    float bladeRand = fract(sin(inInstancePos.x * 12.9898 + inInstancePos.z * 78.233 + inInstancePos.y * 45.164) * 43758.5453);
    outBladeRand = bladeRand;

    // ---- Scaling ----
    vec3 dimensionModifier = vec3(0.5, 2.2, 0.5);
    vec3 scaledPos = localPos * (inInstanceScale * dimensionModifier);

    // ---- Taper: noticeably wider at base, very narrow at tip ----
    float baseWidth = 2.0;   // 100% wider at the ground
    float tipWidth  = 0.1;   // almost a point at the top
    float taper = mix(baseWidth, tipWidth, heightNorm * heightNorm);
    scaledPos.x *= taper;
    scaledPos.z *= taper;

    // ---- Static curvature with per‑blade variation ----
    float bladeHeight = inInstanceScale.y * dimensionModifier.y;
    float bendAmount = 0.15 + bladeRand * 0.25;
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
    gust = gust * 0.4 + 0.8;          // 0.8..1.2
    float finalWindStrength = push.windStrength * gust;

    // Per‑blade wind multiplier
    float windMultiplier = 0.7 + 0.6 * bladeRand;
    float windScale = 1.0 - push.lodFactor * 0.8;

    rotatedPos.x += totalWave * finalWindStrength * windScale * bendFactor * windMultiplier;
    rotatedPos.z += totalWave * finalWindStrength * 0.4  * windScale * bendFactor * windMultiplier;
    rotatedPos.y -= (totalWave * totalWave) * finalWindStrength * 0.15 * windScale * bendFactor * windMultiplier * 0.7;

    outWorldPos = rotatedPos + inInstancePos;
    gl_Position = ubo.proj * ubo.view * vec4(outWorldPos, 1.0);
}