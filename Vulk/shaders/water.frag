#version 450

#include "common_structures.glsl"

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec3 fragNormal;
layout(location = 4) in vec4 fragTangent;

layout(push_constant) uniform PushConstants {
    vec3 cameraPos;
    float time;
    vec2 renderSize;
    vec2 sceneUvScale;
} pc;

layout(set = 1, binding = 0) uniform sampler2D sceneDepth;

layout(location = 0) out vec4 outColor;

float WaveHeight(vec2 position, float time) {
    float broadSwellA = sin(dot(position, vec2(0.010, 0.006)) + time * 0.28);
    float broadSwellB = sin(dot(position, vec2(-0.007, 0.012)) - time * 0.21);

    float windRippleA = sin(dot(position, vec2(0.085, 0.041)) + time * 1.05);
    float windRippleB = sin(dot(position, vec2(-0.054, 0.097)) - time * 0.82);

    float smallRippleA = sin(dot(position, vec2(0.34, 0.19)) + time * 2.10);
    float smallRippleB = sin(dot(position, vec2(-0.21, 0.37)) - time * 1.75);

    return broadSwellA * 0.80 +
           broadSwellB * 0.55 +
           windRippleA * 0.16 +
           windRippleB * 0.12 +
           smallRippleA * 0.035 +
           smallRippleB * 0.025;
}

vec3 CalculateWaterNormal(vec2 position, float time, float detailFade) {
    vec2 slope = vec2(0.0);

    vec2 directionA = normalize(vec2(0.92, 0.38));
    vec2 directionB = normalize(vec2(-0.34, 0.94));
    vec2 directionC = normalize(vec2(0.71, -0.70));
    vec2 directionD = normalize(vec2(-0.96, -0.27));

    float phaseA = dot(position, directionA) * 0.030 + time * 0.34;
    float phaseB = dot(position, directionB) * 0.047 - time * 0.26;
    float phaseC = dot(position, directionC) * 0.110 + time * 0.82;
    float phaseD = dot(position, directionD) * 0.185 - time * 1.15;

    slope += directionA * cos(phaseA) * 0.030 * 0.85;
    slope += directionB * cos(phaseB) * 0.047 * 0.48;
    slope += directionC * cos(phaseC) * 0.110 * 0.15;
    slope += directionD * cos(phaseD) * 0.185 * 0.07;

    float gust = sin(dot(position, vec2(0.016, -0.022)) + time * 0.16);
    float gustStrength = smoothstep(-0.55, 0.85, gust);

    slope += directionA * sin(phaseC * 1.35 + gust * 1.8) * 0.015 * gustStrength;
    slope += directionB * cos(phaseD * 0.72 - gust * 2.2) * 0.010 * gustStrength;

    return normalize(vec3(
        -slope.x * 1.35 * detailFade,
        1.0,
        -slope.y * 1.35 * detailFade
    ));
}

float CalculateShorelineFoam(vec2 position, float waterDepth, float time) {
    float shoreline = 1.0 - smoothstep(0.10, 2.25, waterDepth);

    float broadNoise = sin(dot(position, vec2(0.10, 0.07)) + time * 0.35);
    float fineNoise = sin(dot(position, vec2(-0.33, 0.25)) - time * 0.70);
    float foamNoise = broadNoise * 0.65 + fineNoise * 0.35;

    float brokenBand = smoothstep(-0.45, 0.40, foamNoise);

    return shoreline * mix(0.30, 1.0, brokenBand);
}

void main() {
    ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
    float opaqueDepth = texelFetch(sceneDepth, pixelCoord, 0).r;

    // 1. Reconstruct the exact world position of the opaque terrain behind the water
    vec2 screenUv = gl_FragCoord.xy / pc.renderSize;
    vec2 ndc = screenUv * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, opaqueDepth, 1.0);
    vec4 worldPosOpaque = ubo.inverseViewProj * clipPos;
    worldPosOpaque /= worldPosOpaque.w;

    // 2. Calculate linear distances to bypass non-linear Z-buffer precision entirely
    float distOpaque = length(worldPosOpaque.xyz - ubo.cameraPos);
    float distWater = length(fragPos - ubo.cameraPos);
    float depthDiff = distOpaque - distWater;

    // 3. Hard discard ONLY if terrain is clearly in front of the water (e.g., rocks blocking the view)
    if (opaqueDepth < 0.99999 && depthDiff < -0.15) {
        discard;
    }

    // 4. Create a "Soft Particle" depth blend (0.0 to 1.0) for the physical intersection edge
    // This transitions the 35 centimeters right where the meshes collide into a smooth fade.
    float intersectionFade = clamp((depthDiff + 0.15) / 0.35, 0.0, 1.0);

    // 5. Get the stable vertex depth
    float waterDepth = fragTangent.w;

    vec3 cameraToWater = ubo.cameraPos - fragPos;
    float distanceToCamera = length(cameraToWater);

    float rippleFade = 1.0 - smoothstep(350.0, 1400.0, distanceToCamera);

    vec3 waterNormal = CalculateWaterNormal(fragPos.xz, pc.time, rippleFade);
    vec3 viewDirection = normalize(cameraToWater);
    vec3 lightDirection = normalize(ubo.sunDirection.xyz);
    vec3 halfDirection = normalize(viewDirection + lightDirection);

    float depthFactor = smoothstep(0.0, 1.0, clamp(waterDepth / 8.0, 0.0, 1.0));
    vec3 reflectedVec = reflect(-viewDirection, waterNormal);
    float skyGradient = clamp(reflectedVec.y, 0.0, 1.0);
    float horizonFactor = pow(1.0 - skyGradient, 2.0);

    vec3 shallowBlue = vec3(0.16, 0.68, 0.72);
    vec3 normalBlue = vec3(0.045, 0.31, 0.43);
    vec3 deepBlue = vec3(0.006, 0.050, 0.120);

    float shallowBlend = 1.0 - smoothstep(0.20, 5.50, waterDepth);
    float deepBlend = smoothstep(3.50, 11.00, waterDepth);

    vec3 waterColor = mix(normalBlue, shallowBlue, shallowBlend);
    waterColor = mix(waterColor, deepBlue, deepBlend);

    vec3 horizonReflection = vec3(0.38, 0.58, 0.74);
    vec3 overheadReflection = vec3(0.11, 0.23, 0.37);
    vec3 skyReflection = mix(overheadReflection, horizonReflection, horizonFactor);

    // Clamp the Fresnel effect so the water retains its base color at grazing angles
    float fresnel = pow(1.0 - max(dot(viewDirection, waterNormal), 0.0), 5.0);
    fresnel = mix(0.05, 0.40, fresnel);

    float diffuse = max(dot(waterNormal, lightDirection), 0.0);
    float specular = pow(max(dot(waterNormal, halfDirection), 0.0), 150.0);
    specular *= 0.32 * rippleFade;

    float windBands = sin(dot(fragPos.xz, vec2(0.018, -0.024)) + pc.time * 0.16);
    float windTint = smoothstep(-0.45, 0.75, windBands) * rippleFade;

    vec3 finalColor = mix(waterColor, skyReflection, fresnel);
    finalColor *= mix(0.96, 1.04, windTint * 0.35);
    finalColor += waterColor * diffuse * 0.12;
    finalColor += ubo.sunColor.rgb * specular;

    float shorelineBand = 1.0 - smoothstep(0.05, 1.25, waterDepth);

    float foamNoiseA = sin(dot(fragPos.xz, vec2(0.105, 0.075)) + pc.time * 0.32);
    float foamNoiseB = sin(dot(fragPos.xz, vec2(-0.180, 0.130)) - pc.time * 0.48);
    float foamPattern = 0.5 + 0.5 * (foamNoiseA * 0.65 + foamNoiseB * 0.35);

    float foam = shorelineBand * mix(0.22, 0.70, foamPattern);
    foam *= 1.0 - smoothstep(220.0, 650.0, distanceToCamera);

    finalColor = mix(finalColor, vec3(0.88, 0.96, 0.98), foam * 0.60);

    float fogRange = max(ubo.fogEnd - ubo.fogStart, 0.001);
    float fogFactor = clamp((distanceToCamera - ubo.fogStart) / fogRange, 0.0, 1.0);

    finalColor = mix(finalColor, vec3(0.55, 0.65, 0.75), fogFactor);

    float shallowAlpha = 0.58;
    float deepAlpha = 0.88;

    float alpha = mix(shallowAlpha, deepAlpha, depthFactor);
    alpha = mix(alpha, 0.90, foam);

    // Smooth shore fade: Offset deeper underground to hide geometry gaps
    float shoreFade = smoothstep(-0.60, -0.05, waterDepth);
    alpha *= shoreFade;

    alpha *= (opaqueDepth < 0.99999) ? intersectionFade : 1.0;

    outColor = vec4(finalColor, alpha);
}