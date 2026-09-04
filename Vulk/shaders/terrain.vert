// terrain.vert
#version 450
#extension GL_ARB_shader_draw_parameters : enable
#include "common_structures.glsl"

layout(push_constant) uniform Constants {
    mat4 modelMatrix;
    uint textureId;
    uint normalTextureId;
    uint objectId;
    float morphBlend;  
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inNormal;             // SNORM vec2
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec3 inColor;
layout(location = 5) in float inCoarseY;           // Replaced inCoarsePos
layout(location = 6) in vec2 inCoarseNormal;       // SNORM vec2
layout(location = 7) in vec4 inCoarseTangent;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint fragTextureId;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out float fragTangentHandedness;
layout(location = 6) flat out uint fragNormalTextureId;
layout(location = 7) out vec3 fragColor;

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
    uint baseInstance = gl_BaseInstanceARB;
    float blend = uintBitsToFloat(baseInstance);

    vec3 decodedNormal = decodeNormal(inNormal);
    vec3 decodedCoarseNormal = decodeNormal(inCoarseNormal);

    // 1. Morph Y-axis only
    vec3 finalPos = inPosition;
    finalPos.y = mix(inPosition.y, inCoarseY, blend);
    
    // 2. Morph Normals
    vec3 finalNormal = normalize(mix(decodedNormal, decodedCoarseNormal, blend));
    
    // 3. Morph Materials (Biomes)
    vec4 finalTangent = mix(inTangent, inCoarseTangent, blend);

    vec4 worldPos = push.modelMatrix * vec4(finalPos, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    mat3 normalMatrix = transpose(inverse(mat3(push.modelMatrix)));
    vec3 worldNormal = normalize(normalMatrix * finalNormal);

    float slopeFactor = max(dot(worldNormal, vec3(0.0, 1.0, 0.0)), 0.0);
    float cliffWeight = smoothstep(0.5, 0.8, slopeFactor);
    
    vec3 blendedWeights = finalTangent.xyz; // Use the geomorphed material weights
    blendedWeights.r *= cliffWeight;
    blendedWeights.g *= cliffWeight;
    blendedWeights.b = max(blendedWeights.b, 1.0 - cliffWeight);
    float totalWeight = blendedWeights.r + blendedWeights.g + blendedWeights.b;
    
    fragTangent = blendedWeights / max(totalWeight, 0.0001);
    fragColor = inColor;
    fragNormal = normalMatrix * finalNormal;
    fragTangentHandedness = inTangent.w; 
    fragTexCoord = inTexCoord;
    fragTextureId = push.textureId;
    fragNormalTextureId = push.normalTextureId;
    fragWorldPos = worldPos.xyz;
}