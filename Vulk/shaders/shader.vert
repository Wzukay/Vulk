#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    float ambient;
    vec4 fadeParams;
    vec2 screenSize;
    float specularPower;
    uint lightCount;
    float fogStart;
    float fogEnd;
    mat4 inverseViewProj;
    mat4 inverseProj;
    mat4 inverseView;
} ubo;

layout(push_constant) uniform Constants {
    mat4 modelMatrix;
    uint textureId;
    uint normalTextureId;
    uint objectId;
    float lodBlend; 
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
    vec3 finalPos = mix(inPosition, inCoarsePos, push.lodBlend);
    vec3 finalNormal = mix(inNormal, inCoarseNormal, push.lodBlend);

    vec4 worldPos = push.modelMatrix * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    mat3 normalMatrix = transpose(inverse(mat3(push.modelMatrix)));
    vec3 worldNormal = normalize(normalMatrix * inNormal);

    float slopeFactor = max(dot(worldNormal, vec3(0.0, 1.0, 0.0)), 0.0);
    float cliffWeight = smoothstep(0.5, 0.8, slopeFactor);

    vec3 blendedWeights = inColor;
    blendedWeights.r *= cliffWeight;
    blendedWeights.g *= cliffWeight;
    blendedWeights.b = max(blendedWeights.b, 1.0 - cliffWeight);

    float totalWeight = blendedWeights.r + blendedWeights.g + blendedWeights.b;
    fragColor = blendedWeights / max(totalWeight, 0.0001);

    fragNormal = normalMatrix * inNormal;
    fragTangent = normalMatrix * inTangent.xyz;
    fragTangentHandedness = inTangent.w;

    fragTexCoord = inTexCoord;
    fragTextureId = push.textureId;
    fragNormalTextureId = push.normalTextureId;
    fragWorldPos = worldPos.xyz;
}