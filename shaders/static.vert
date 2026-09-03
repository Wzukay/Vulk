#version 450
#include "common_structures.glsl"

layout(push_constant) uniform Constants {
    mat4 modelMatrix;
    uint textureId;
    uint normalTextureId;
    uint objectId;
    float lodBlend; 
    uint ormTextureId; // <-- NEW
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
layout(location = 7) flat out uint fragOrmTextureId;

void main() {
    vec4 worldPos = push.modelMatrix * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    mat3 normalMatrix = transpose(inverse(mat3(push.modelMatrix)));
    
    fragNormal = normalMatrix * inNormal;
    fragTangent = normalMatrix * inTangent.xyz;
    fragTangentHandedness = inTangent.w;
    
    fragTexCoord = inTexCoord;
    fragTextureId = push.textureId;
    fragNormalTextureId = push.normalTextureId;
    fragWorldPos = worldPos.xyz;
    
    fragOrmTextureId = push.ormTextureId;
}