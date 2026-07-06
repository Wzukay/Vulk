#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    float ambient;
    float specularPower;
    uint lightCount;
} ubo;

layout(push_constant) uniform Constants {
    mat4 modelMatrix;
    uint textureId;
    uint objectId;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint fragTextureId;
layout(location = 3) out vec3 fragWorldPos;

void main() {
    vec4 worldPos = push.modelMatrix * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    mat3 normalMatrix = transpose(inverse(mat3(push.modelMatrix)));
    fragNormal = normalMatrix * inNormal;
    fragTexCoord = inTexCoord;
    fragTextureId = push.textureId;
    fragWorldPos = worldPos.xyz;
}