#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneTexture;
layout(binding = 1) uniform sampler2D ssaoMap;

// Must match fsr.frag identically to avoid push constant size mismatch crashes
layout(push_constant) uniform FSRPushConstants {
    vec4 Const0;
    vec4 Const1;
    vec4 Const2;
    vec4 Const3;
    float sharpness;
    uint enableSSAO;
    vec2 _pad;
} pc;

void main() {
    vec4 color = texture(sceneTexture, fragUV);
    
    // Multiply the contact shadows into the scene before hitting the swapchain
    if (pc.enableSSAO == 1) {
        float ssaoShadow = texture(ssaoMap, fragUV).r;
        color.rgb *= ssaoShadow;
    }
    
    outColor = color;
}