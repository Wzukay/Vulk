#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    float ambient;
    float specularPower;
    uint lightCount;
    float fogStart;
    float fogEnd;
    vec2 screenSize;
    mat4 inverseViewProj;
    mat4 inverseProj;
    mat4 inverseView;
} ubo;

layout(binding = 4) uniform samplerCube skyboxCube;

void main() {
    vec4 clipPos = vec4(inUV, 1.0, 1.0);
    vec4 viewPos = ubo.inverseProj * clipPos;
    vec3 viewDirView = viewPos.xyz / viewPos.w;                 // divide per-pixel now
    vec3 viewDir = normalize(mat3(ubo.inverseView) * viewDirView); // normalize per-pixel now

    outColor = texture(skyboxCube, viewDir);
}