#version 450

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
    // Define the colors for the root and the tip of the grass
    vec3 bottomColor = vec3(0.02, 0.15, 0.03); // Darker, earthier green near the dirt
    vec3 topColor    = vec3(0.35, 0.65, 0.15); // Bright, vibrant green at the tip
    
    // fragUV.y is 0.0 at the tip and 1.0 at the root. 
    // We add a slight power curve so the bright tip color pushes further down the blade.
    float gradient = pow(fragUV.y, 0.8);
    vec3 baseColor = mix(topColor, bottomColor, gradient);
    
    // 2. Fake Lighting
    // Basic ambient light shading based on a hardcoded sun/sky direction
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diff = max(dot(fragNormal, lightDir), 0.35); // 0.35 represents the ambient shadow floor
    
    vec3 finalColor = baseColor * diff;

    // 3. Output
    // Since the geometry physically narrows to a point, we don't need alpha clipping.
    // We output a solid 1.0 alpha.
    outColor = vec4(finalColor, 1.0);
}