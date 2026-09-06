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

void main() {
    ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
    float opaqueDepth = texelFetch(sceneDepth, pixelCoord, 0).r;

    // 1. Reconstruct the exact world position of the opaque terrain behind the water
    vec2 screenUv = gl_FragCoord.xy / pc.renderSize;
    vec2 ndc = screenUv * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, opaqueDepth, 1.0);
    vec4 worldPosOpaque = ubo.inverseViewProj * clipPos;
    worldPosOpaque /= worldPosOpaque.w;

    // 2. Calculate linear distances
    vec3 cameraToWater = ubo.cameraPos - fragPos;
    float distanceToCamera = length(cameraToWater);
    float distOpaque = length(worldPosOpaque.xyz - ubo.cameraPos);
    float depthDiff = distOpaque - distanceToCamera;

    // 3. FIXED: Use fragPos.y directly without wave offset (wave removed from vertex shader)
    // This prevents the wave displacement mismatch that caused the jitter
    float stableVerticalDepth = (opaqueDepth >= 0.99999) ? 10.0 : max(fragPos.y - worldPosOpaque.y, 0.01);

    // 4. Create a "Soft Particle" depth blend with wider transition to reduce sensitivity
    float intersectionFade = clamp((depthDiff + 0.5) / 1.0, 0.0, 1.0);

    // 5. FIXED: Shore Stability Zone
    // This creates a stable region at the shore where depth calculations are reliable
    float shoreStabilityZone = smoothstep(2.0, 0.1, stableVerticalDepth);
    float stableBaseDepth = 0.5;
    
    // 6. FIXED: Hybrid Depth with Shore Clamping
    // Use only vertex depth near camera (shore), transition to pixel depth farther away
    float pixelVerticalDepth = (opaqueDepth < 0.99999) ? max(fragPos.y - worldPosOpaque.y, 0.0) : 10.0;
    float vertexDepth = fragTangent.w * 30.0;
    
    // Blend transition: shore (close) uses more vertex depth, far uses pixel depth
    // BUT clamp ranges to prevent extreme values
    pixelVerticalDepth = clamp(pixelVerticalDepth, 0.01, 50.0);
    vertexDepth = clamp(vertexDepth, 0.1, 50.0);
    
    float depthBlendFactor = smoothstep(20.0, 100.0, distanceToCamera);
    float visualWaterDepth = mix(pixelVerticalDepth, vertexDepth, depthBlendFactor);

    // 7. FIXED: Apply shore stability smoothing
    // At the shore, blend toward a stable baseline to eliminate flicker
    visualWaterDepth = mix(visualWaterDepth, stableBaseDepth, shoreStabilityZone * 0.6);
    
    // Final clamp to safe range
    visualWaterDepth = clamp(visualWaterDepth, 0.1, 50.0);

    // 8. Ripple detail fade
    float rippleFade = 1.0 - smoothstep(350.0, 1400.0, distanceToCamera);

    // 9. Normal calculation
    vec3 waterNormal = CalculateWaterNormal(fragPos.xz, pc.time, rippleFade);
    vec3 viewDirection = normalize(cameraToWater);
    vec3 lightDirection = normalize(ubo.sunDirection.xyz);
    vec3 halfDirection = normalize(viewDirection + lightDirection);

    // 10. Depth-based coloring with wider smoothstep ranges (less sensitive to jitter)
    float depthFactor = smoothstep(0.0, 8.0, visualWaterDepth);
    
    vec3 reflectedVec = reflect(-viewDirection, waterNormal);
    float skyGradient = clamp(reflectedVec.y, 0.0, 1.0);
    float horizonFactor = pow(1.0 - skyGradient, 2.0);

    vec3 shallowBlue = vec3(0.16, 0.68, 0.72);
    vec3 normalBlue = vec3(0.045, 0.31, 0.43);
    vec3 deepBlue = vec3(0.006, 0.050, 0.120);

    // FIXED: Wider smoothstep ranges to reduce sensitivity at shore
    float shallowBlend = 1.0 - smoothstep(0.10, 3.0, visualWaterDepth);
    float deepBlend = smoothstep(2.0, 8.0, visualWaterDepth);

    vec3 waterColor = mix(normalBlue, shallowBlue, shallowBlend);
    waterColor = mix(waterColor, deepBlue, deepBlend);

    vec3 horizonReflection = vec3(0.38, 0.58, 0.74);
    vec3 overheadReflection = vec3(0.11, 0.23, 0.37);
    vec3 skyReflection = mix(overheadReflection, horizonReflection, horizonFactor);

    // Fresnel effect with clamping
    float fresnel = pow(1.0 - max(dot(viewDirection, waterNormal), 0.0), 5.0);
    fresnel = mix(0.05, 0.40, fresnel);

    float diffuse = max(dot(waterNormal, lightDirection), 0.0);
    float specular = pow(max(dot(waterNormal, halfDirection), 0.0), 150.0);
    specular *= 0.32 * rippleFade;

    // Wind bands
    float windBands = sin(dot(fragPos.xz, vec2(0.018, -0.024)) + pc.time * 0.16);
    float windTint = smoothstep(-0.45, 0.75, windBands) * rippleFade;

    vec3 baseSurfaceColor = mix(waterColor, skyReflection, fresnel);
    baseSurfaceColor *= mix(0.96, 1.04, windTint * 0.35);

    vec3 diffuseLight = diffuse * ubo.sunColor.rgb;
    vec3 ambientLight = vec3(ubo.ambient) * 3.5;

    vec3 finalColor = baseSurfaceColor * (diffuseLight * 0.2 + ambientLight);
    finalColor += ubo.sunColor.rgb * specular;

    // 11. FIXED: Shore foam with reduced sensitivity
    // Wider transition band and reduced near shore
    float shorelineBand = 1.0 - smoothstep(0.05, 2.5, visualWaterDepth);

    float foamNoiseA = sin(dot(fragPos.xz, vec2(0.105, 0.075)) + pc.time * 0.32);
    float foamNoiseB = sin(dot(fragPos.xz, vec2(-0.180, 0.130)) - pc.time * 0.48);
    float foamPattern = 0.5 + 0.5 * (foamNoiseA * 0.65 + foamNoiseB * 0.35);

    // FIXED: Reduce foam at shore where it flickers most
    float foam = shorelineBand * mix(0.22, 0.70, foamPattern);
    foam *= 1.0 - smoothstep(220.0, 650.0, distanceToCamera);
    foam *= (1.0 - shoreStabilityZone * 0.5);  // Less foam at shore

    // Lit foam color
    vec3 litFoamColor = vec3(0.88, 0.96, 0.98) * (diffuseLight * 0.2 + ambientLight);
    finalColor = mix(finalColor, litFoamColor, foam * 0.60);

    // --- FOG ---
    float fogRange = max(ubo.fogEnd - ubo.fogStart, 0.001);
    float fogFactor = clamp((distanceToCamera - ubo.fogStart) / fogRange, 0.0, 1.0);

    vec3 litFogColor = vec3(0.55, 0.65, 0.75) * (ambientLight * 0.8);
    finalColor = mix(finalColor, litFogColor, fogFactor);

    // --- ALPHA & DEPTH DISCARD ---
    float shallowAlpha = 0.15;
    float deepAlpha = 0.95;

    float alpha = mix(shallowAlpha, deepAlpha, depthFactor);
    alpha = mix(alpha, 0.90, foam);

    // FIXED: Use intersection fade with wider transition to prevent alpha jitter
    alpha *= intersectionFade;

    outColor = vec4(finalColor, alpha);
}
