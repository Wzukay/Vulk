#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneTexture;
layout(binding = 1) uniform sampler2D ssaoMap;

layout(push_constant) uniform FSRPushConstants {
    vec4 Const0;
    vec4 Const1;
    vec4 Const2;
    vec4 Const3;
    float sharpness;
    uint enableSSAO;
    vec2 renderScale;
} pc;

void main() {
    vec2 texSize = vec2(textureSize(sceneTexture, 0));
    vec2 invTexSize = 1.0 / texSize; // Step size is exactly 1 physical pixel
    
    vec2 activeUV = fragUV * pc.renderScale;
    
    vec3 finalRGB;
    float alpha = texture(sceneTexture, activeUV).a;

    // Skip sharpening if turned off
    if (pc.sharpness <= 0.0) {
        finalRGB = texture(sceneTexture, activeUV).rgb;
    } else {
        // 1. Clean Bicubic/Linear Upscale fetch
        vec3 c = texture(sceneTexture, activeUV).rgb;

        // 2. RCAS - Sample surrounding pixels safely using physical pixel steps
        vec3 b = texture(sceneTexture, activeUV + vec2( 0.0, -invTexSize.y)).rgb;
        vec3 l = texture(sceneTexture, activeUV + vec2(-invTexSize.x,  0.0)).rgb;
        vec3 r = texture(sceneTexture, activeUV + vec2( invTexSize.x,  0.0)).rgb;
        vec3 t = texture(sceneTexture, activeUV + vec2( 0.0,  invTexSize.y)).rgb;

        // Constrain sharpening limits to prevent glowing edge artifacts
        vec3 mn = min(min(min(b, l), r), t);
        vec3 mx = max(max(max(b, l), r), t);
        
        vec3 center = c;
        vec3 node = 2.0 * center;
        
        // Controlled contrast adaptation using user-defined sharpness
        float safeSharpness = clamp(pc.sharpness, 0.0f, 1.0f) * 0.2f;
        vec3 w = clamp(min(mn, 2.0 - mx) / max(mx, 0.00001), 0.0, 1.0);
        w = inversesqrt(w) * -safeSharpness;

        finalRGB = clamp((node + b * w + l * w + r * w + t * w) / (1.0 + 4.0 * w), 0.0, 1.0);
    }

    // Multiply the contact shadows into the scene before hitting the swapchain
    if (pc.enableSSAO == 1) {
        float ssaoShadow = texture(ssaoMap, activeUV).r;
        finalRGB *= ssaoShadow;
    }

    outColor = vec4(finalRGB, alpha);
}