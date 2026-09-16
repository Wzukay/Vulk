#version 450
#include "common_structures.glsl"

layout(location = 0) out vec3 outViewDir;

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 clipSpace = uv * 2.0 - 1.0;
    
    // Standard Vulkan NDC (Z = 1.0 puts the skybox at the far depth plane)
    gl_Position = vec4(clipSpace, 1.0, 1.0);

    // 1. Un-project the clip space coordinate to view space
    vec4 viewPos = ubo.inverseProj * vec4(clipSpace, 1.0, 1.0);
    
    // 2. Multiply by the inverse view matrix, but cast it to a mat3!
    // A mat3 strips out the translation (position) and only applies the rotation.
    outViewDir = mat3(ubo.inverseView) * (viewPos.xyz / viewPos.w);
}