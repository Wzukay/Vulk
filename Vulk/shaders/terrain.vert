#version 450
#include "common_structures.glsl"

layout(push_constant) uniform Constants {
    mat4 modelMatrix;
    uint textureId;
    uint normalTextureId;
    uint objectId;
    // We no longer need lodBlend from push constants, but we leave the struct size alone
    float _padBlend; 
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec3 inColor;
layout(location = 5) in vec3 inCoarsePos;
layout(location = 6) in vec3 inCoarseNormal; 

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint fragTextureId;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out float fragTangentHandedness;
layout(location = 6) flat out uint fragNormalTextureId;
layout(location = 7) out vec3 fragColor;

void main() {
    // --- THE UNPACK: Uint-to-Float bitcast ---
    float dynamicLodBlend = uintBitsToFloat(gl_InstanceIndex);

    vec3 finalPos = mix(inPosition, inCoarsePos, dynamicLodBlend);
    vec3 finalNormal = mix(inNormal, inCoarseNormal, dynamicLodBlend);

    vec4 worldPos = push.modelMatrix * vec4(finalPos, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    mat3 normalMatrix = transpose(inverse(mat3(push.modelMatrix)));
    vec3 worldNormal = normalize(normalMatrix * finalNormal);

    float slopeFactor = max(dot(worldNormal, vec3(0.0, 1.0, 0.0)), 0.0);
    float cliffWeight = smoothstep(0.5, 0.8, slopeFactor);

    vec3 blendedWeights = inColor;
    blendedWeights.r *= cliffWeight;
    blendedWeights.g *= cliffWeight;
    blendedWeights.b = max(blendedWeights.b, 1.0 - cliffWeight);

    float totalWeight = blendedWeights.r + blendedWeights.g + blendedWeights.b;
    fragColor = blendedWeights / max(totalWeight, 0.0001);

    fragNormal = normalMatrix * finalNormal;
    fragTangent = normalMatrix * inTangent.xyz;
    fragTangentHandedness = inTangent.w;

    fragTexCoord = inTexCoord;
    fragTextureId = push.textureId;
    fragNormalTextureId = push.normalTextureId;
    fragWorldPos = worldPos.xyz;
}