#version 450
#include "common_structures.glsl"

layout(push_constant) uniform Constants {
    mat4 modelMatrix;
    uint textureId;
    uint normalTextureId;
    uint objectId;  
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inNormal; // Decoded via decodeNormal(inNormal)
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec3 inColor; 

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint fragTextureId;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out float fragTangentHandedness;
layout(location = 6) flat out uint fragNormalTextureId;

// --- Octahedral Decoding ---
vec2 octWrap(vec2 v) {
    return (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec3 decodeNormal(vec2 f) {
    vec3 n = vec3(f.x, 1.0 - abs(f.x) - abs(f.y), f.y);
    float t = clamp(-n.y, 0.0, 1.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.z += n.z >= 0.0 ? -t : t;
    return normalize(n);
}

void main() {
    vec4 worldPos = push.modelMatrix * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    mat3 normalMatrix = transpose(inverse(mat3(push.modelMatrix)));
    
    vec3 decodedNormal = decodeNormal(inNormal);
    
    fragNormal = normalMatrix * decodedNormal;
    fragTangent = normalMatrix * inTangent.xyz;
    fragTangentHandedness = inTangent.w;
    
    fragTexCoord = inTexCoord;
    fragTextureId = push.textureId;
    fragNormalTextureId = push.normalTextureId;
    fragWorldPos = worldPos.xyz;
}