#version 450
#include "common_structures.glsl"

layout(location = 0) out vec3 outViewDir;

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 clipSpace = uv * 2.0 - 1.0;
    
    // Standard Vulkan NDC
    gl_Position = vec4(clipSpace, 1.0, 1.0);

    vec4 worldPos = ubo.inverseViewProj * vec4(clipSpace, 1.0, 1.0);
    
    outViewDir = (worldPos.xyz / worldPos.w) - ubo.cameraPos;
}