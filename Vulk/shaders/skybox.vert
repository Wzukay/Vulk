#version 450
layout(location = 0) out vec3 outViewDir;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;         // <--- CHANGED BACK TO VEC3!
    float ambient;
    vec4 fadeParams;
    vec2 screenSize;
    float specularPower;
    uint lightCount;
    float fogStart;
    float fogEnd;
    vec2 _pad2;
    vec4 sunDirection;
    vec4 sunColor;
    mat4 inverseViewProj;
    mat4 inverseProj;
    mat4 inverseView;
} ubo;

void main() {
    // Generate full screen triangle
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    
    // Z = 1.0 pushes the skybox to the absolute back of the depth buffer
    gl_Position = vec4(uv * 2.0 - 1.0, 1.0, 1.0); 
    
    // Calculate the 3D world direction we are looking at for this pixel
    vec4 target = ubo.inverseProj * vec4(gl_Position.x, gl_Position.y, 1.0, 1.0);
    
    // FIX: Removed normalize() so the GPU interpolates the flat plane correctly!
    outViewDir = (ubo.inverseView * vec4(target.xyz / target.w, 0.0)).xyz;
}