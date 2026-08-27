#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// This will hold your off-screen 3D scene
layout(binding = 0) uniform sampler2D sceneTexture;

void main() {
    outColor = texture(sceneTexture, fragUV);
}