#version 450

#include "common_structures.glsl"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint fragTextureId;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in float fragTangentHandedness;
layout(location = 6) flat in uint fragNormalTextureId;
layout(location = 7) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

vec3 ApplyFog(vec3 color, float distanceToCamera) {
    float fogRange = max(ubo.fogEnd - ubo.fogStart, 0.001);
    float fogFactor = clamp((distanceToCamera - ubo.fogStart) / fogRange, 0.0, 1.0);
    return mix(color, vec3(0.6, 0.7, 0.8), fogFactor);
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453123);
}

float valueNoise(vec2 x) {
    vec2 i = floor(x);
    vec2 f = fract(x);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

void main() {
    float distanceToCamera = length(ubo.cameraPos - fragWorldPos);

    vec3 blendWeights = max(fragColor, vec3(0.0));
    float weightSum = blendWeights.r + blendWeights.g + blendWeights.b;
    blendWeights /= max(weightSum, 0.0001);

    vec3 normal = normalize(fragNormal);

    // --- NEW: PROCEDURAL NORMAL PERTURBATION (Up-Close Only) ---
    // We fade the bump mapping out completely past 150 units to save ALU
    // and prevent nasty high-frequency pixel aliasing in the distance.
    float detailFade = clamp(1.0 - (distanceToCamera / 150.0), 0.0, 1.0);
    
    if (detailFade > 0.0) {
        float bumpFreq = 2.5; // Controls the size of the pebbles/bumps
        float eps = 0.05;
        vec2 p = fragWorldPos.xz * bumpFreq;
        
        // Cheap finite difference to get the slope of the noise
        float hL = valueNoise(p - vec2(eps, 0.0));
        float hR = valueNoise(p + vec2(eps, 0.0));
        float hD = valueNoise(p - vec2(0.0, eps));
        float hU = valueNoise(p + vec2(0.0, eps));
        
        vec2 gradient = vec2(hR - hL, hU - hD) / (2.0 * eps);
        
        // Dynamic bump strength: Rocks are jagged (high), Grass is bumpy (mid), Sand is smooth (low)
        float matBump = 0.05 * blendWeights.r + 0.3 * blendWeights.g + 0.9 * blendWeights.b;
        float finalBump = matBump * detailFade;
        
        // Apply the gradient to the normal's X and Z axes
        normal.x -= gradient.x * finalBump;
        normal.z -= gradient.y * finalBump;
        normal = normalize(normal);
    }
    // -----------------------------------------------------------

    // 1. HEIGHT & SLOPE CALCULATION
    float slope = clamp(normal.y, 0.0, 1.0);
    float elevation = clamp(fragWorldPos.y / 150.0, 0.0, 1.0);

    // 2. DYNAMIC ALU COLOR VARIATION
    float n = valueNoise(fragWorldPos.xz * 0.02);
    float noisyElevation = clamp(elevation + (n * 0.3 - 0.15), 0.0, 1.0);

    vec3 sandColor = mix(vec3(0.40, 0.28, 0.15), vec3(0.55, 0.40, 0.22), noisyElevation);
    
    vec3 lushGrass = vec3(0.10, 0.30, 0.06);
    vec3 dryGrass  = vec3(0.25, 0.35, 0.10);
    vec3 grassColor = mix(lushGrass, dryGrass, noisyElevation) * mix(0.6, 1.0, slope);

    vec3 darkRock  = vec3(0.15, 0.16, 0.17);
    vec3 lightRock = vec3(0.35, 0.36, 0.38);
    vec3 rockColor = mix(darkRock, lightRock, noisyElevation);

    float snowMask = smoothstep(0.7, 0.9, noisyElevation) * smoothstep(0.6, 0.9, slope);
    rockColor = mix(rockColor, vec3(0.85, 0.88, 0.92), snowMask);
    
    blendWeights.b = max(blendWeights.b, snowMask); 
    weightSum = blendWeights.r + blendWeights.g + blendWeights.b;
    blendWeights /= max(weightSum, 0.0001);

    // 3. FINAL BLEND
    vec3 terrainColor =
        sandColor * blendWeights.r +
        grassColor * blendWeights.g +
        rockColor * blendWeights.b;

    float colorBreakup = mix(0.85, 1.15, n);
    terrainColor *= colorBreakup;

    // 4. LIGHTING
    vec3 sunDirection = normalize(ubo.sunDirection.xyz);
    
    // Lighting now uses the newly perturbed normal!
    float diffuse = max(dot(normal, sunDirection), 0.0);
    float wrappedDiffuse = diffuse * 0.85 + 0.15;

    vec3 lighting =
        vec3(ubo.ambient) +
        ubo.sunColor.rgb *
        ubo.sunColor.a *
        wrappedDiffuse;

    outColor = vec4(
        ApplyFog(terrainColor * lighting, distanceToCamera),
        1.0);
}