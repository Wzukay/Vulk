#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneTexture;

layout(push_constant) uniform FSRPushConstants {
    vec4 Const0;
    vec4 Const1;
    vec4 Const2;
    vec4 Const3;
    float sharpness;
} pc;

void main() {
    // If sharpness is turned off, do a clean linear sample
    vec2 texSize = vec2(textureSize(sceneTexture, 0));
    vec2 invTexSize = 1.0 / texSize;

    if (pc.sharpness <= 0.0) {
        outColor = texture(sceneTexture, fragUV);
        return;
    }

    // 1. Clean Bicubic/Linear Upscale fetch
    vec3 c = texture(sceneTexture, fragUV).rgb;

    // 2. RCAS - Sample surrounding pixels safely
    vec3 b = texture(sceneTexture, fragUV + vec2( 0.0, -invTexSize.y)).rgb;
    vec3 l = texture(sceneTexture, fragUV + vec2(-invTexSize.x,  0.0)).rgb;
    vec3 r = texture(sceneTexture, fragUV + vec2( invTexSize.x,  0.0)).rgb;
    vec3 t = texture(sceneTexture, fragUV + vec2( 0.0,  invTexSize.y)).rgb;

    // Constrain sharpening limits to prevent glowing edge artifacts
    vec3 mn = min(min(min(b, l), r), t);
    vec3 mx = max(max(max(b, l), r), t);
    
    vec3 center = c;
    vec3 node = 2.0 * center;
    
    // Controlled contrast adaptation using user-defined sharpness (clamped safely)
    float safeSharpness = clamp(pc.sharpness, 0.0f, 1.0f) * 0.2f;
    vec3 w = clamp(min(mn, 2.0 - mx) / max(mx, 0.00001), 0.0, 1.0);
    w = inversesqrt(w) * -safeSharpness;

    vec3 finalRGB = (node + b * w + l * w + r * w + t * w) / (1.0 + 4.0 * w);
    
    // Output the sharpened color directly, letting Vulkan handle sRGB swapchain presentation
    float alpha = texture(sceneTexture, fragUV).a;
    outColor = vec4(clamp(finalRGB, 0.0, 1.0), alpha);
}