#version 450
#include "common_structures.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) out vec3 outNormal;

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;      // identity for now, but kept for flexibility
    float time;
    uint normalTextureId;
    float tiling;
    float waveStrength;
} pc;

// ------------------------------------------------------------------
// Wave parameters – you can promote these to push constants later
// ------------------------------------------------------------------
const uint  WAVE_COUNT = 4;
const vec3  waveDirs[4] = vec3[4](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 0.0, 1.0),
    vec3(0.8, 0.0, 0.6),
    vec3(-0.6, 0.0, 0.8)
);
const float waveFreq[4] = float[4](0.8, 1.2, 1.6, 2.0);
const float waveAmp[4]  = float[4](0.6, 0.4, 0.3, 0.2);
const float waveSpeed[4] = float[4](1.2, 1.5, 1.8, 2.2);

void main() {
    vec3 pos = inPos;
    vec3 normal = vec3(0.0, 1.0, 0.0);   // will be perturbed

    // -------- Wave displacement (Gerstner‑like) --------
    float time = pc.time;
    float strength = pc.waveStrength;

    for (int i = 0; i < WAVE_COUNT; i++) {
        vec3 dir = waveDirs[i];
        float freq = waveFreq[i];
        float amp = waveAmp[i] * strength;
        float speed = waveSpeed[i];

        float phase = freq * (dot(dir, pos)) + speed * time;
        float s = sin(phase);
        float c = cos(phase);

        // Displacement in world Y (up) and horizontal (X/Z) to create rolling waves
        float horizAmp = amp * 0.2;  // gentle horizontal push
        pos += vec3(dir.x * horizAmp * c, amp * s, dir.z * horizAmp * c);

        // Approximate normal perturbation (derivative of displacement)
        vec3 waveNormal = vec3(
            -dir.x * freq * amp * c,
            1.0 - freq * amp * s,
            -dir.z * freq * amp * c
        );
        normal += waveNormal;
    }
    normal = normalize(normal);

    // -------- Transform to world space --------
    vec4 worldPos = pc.modelMatrix * vec4(pos, 1.0);
    outWorldPos = worldPos.xyz;
    outNormal   = normalize((pc.modelMatrix * vec4(normal, 0.0)).xyz);

    outUV = inUV * pc.tiling;   // apply tiling

    gl_Position = ubo.proj * ubo.view * worldPos;
}