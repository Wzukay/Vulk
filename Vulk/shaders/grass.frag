#version 450
#include "common_structures.glsl"

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

// We leave the push constants struct intact so it perfectly matches the C++ layout, 
// even if we don't use textureId anymore.
layout(push_constant) uniform PushConstants {
    float time;
    uint textureId; 
    float windStrength;
    float windSpeed;
    float lodFactor;
} pc;

void main() {
    // 1. Procedural Gradient
    vec3 bottomColor = vec3(0.02, 0.15, 0.03); 
    vec3 topColor    = vec3(0.35, 0.65, 0.15); 
    
    float gradient = pow(fragUV.y, 0.8);
    vec3 baseColor = mix(topColor, bottomColor, gradient);
    
    // 2. Dynamic Lighting
    // Replace the hardcoded light vector with the engine's sun direction
    vec3 lightDir = normalize(ubo.sunDirection.xyz);
    float nDotL = max(dot(fragNormal, lightDir), 0.0); 
    
    // Calculate diffuse lighting multiplied by the sun's color
    vec3 diffuse = nDotL * ubo.sunColor.rgb;
    
    // Combine diffuse with the dynamic ambient floor (which handles the night-time darkness)
    vec3 finalColor = baseColor * (diffuse + vec3(ubo.ambient));

    // 3. Output
    outColor = vec4(finalColor, 1.0);
}