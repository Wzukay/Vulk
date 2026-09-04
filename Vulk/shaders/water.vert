#version 450

#include "common_structures.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inNormal;     // SNORM packed vec2
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec3 inColor;

layout(push_constant) uniform PushConstants {
    vec3 cameraPos;
    float time;
} pc;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec3 fragNormal;
layout(location = 4) out vec3 fragTangent;

// --- Octahedral Decoding ---
vec2 octWrap(vec2 v) {
    return (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec3 decodeNormal(vec2 f) {
    vec3 n = vec3(f.x, 1.0 - abs(f.x) - abs(f.y), f.y);
    float t = clamp(-n.y, 0.0, 1.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.z += n.z >= 0.0 ? -t : t;
    float len = length(n);
    // Protect against NaN from length 0
    return len > 1e-5 ? (n / len) : vec3(0.0, 1.0, 0.0);
}

void main() {
    vec4 worldPos = vec4(inPos, 1.0);
    
    float wave = sin(worldPos.x * 0.2 + pc.time * 2.0) * cos(worldPos.z * 0.2 + pc.time * 1.5) * 0.15;
    worldPos.y += wave;

    gl_Position = ubo.proj * ubo.view * worldPos;
    
    fragPos = worldPos.xyz;
    fragTexCoord = vec2(worldPos.x, worldPos.z) * 0.05; 
    fragColor = inColor;
    
    fragNormal = decodeNormal(inNormal);
    fragTangent = inTangent.xyz; 
}