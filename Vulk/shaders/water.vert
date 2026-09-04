#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inTangent;
layout(location = 5) in vec3 inCoarsePos;
layout(location = 6) in vec3 inCoarseNormal;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    float ambient;
    float specularPower;
    uint lightCount;
    vec3 cameraPos;
    float fogStart;
    float fogEnd;
    vec4 fadeParams;
    vec2 screenSize;
    mat4 inverseViewProj;
    mat4 inverseProj;
    mat4 inverseView;
    vec4 sunDirection;
    vec4 sunColor;
} ubo;

layout(push_constant) uniform PushConstants {
    vec3 cameraPos;
    float time;
} pc;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec3 fragNormal;
layout(location = 4) out vec3 fragTangent;

void main() {
    vec4 worldPos = vec4(inPos, 1.0);
    
    // Subtle vertex wave displacement to make the surface look alive
    float wave = sin(worldPos.x * 0.2 + pc.time * 2.0) * cos(worldPos.z * 0.2 + pc.time * 1.5) * 0.15;
    worldPos.y += wave;

    gl_Position = ubo.proj * ubo.view * worldPos;
    
    fragPos = worldPos.xyz;
    // World-space UVs so the texture scales perfectly across chunk boundaries
    fragTexCoord = vec2(worldPos.x, worldPos.z) * 0.05; 
    fragColor = inColor;
    fragNormal = inNormal;
    fragTangent = inTangent.xyz; 
}