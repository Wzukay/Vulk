#version 450
#include "common_structures.glsl"

layout(push_constant) uniform SkyPush {
    float time;
    float timeScale;
    float starFade;
    float coverage;
    vec4 zenithColor;
    vec4 horizonColor;
} pc;

layout(location = 0) in vec3 inViewDir;
layout(location = 0) out vec4 outColor;

vec2 hash(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(dot(hash(i + vec2(0.0,0.0)), f - vec2(0.0,0.0)), 
                   dot(hash(i + vec2(1.0,0.0)), f - vec2(1.0,0.0)), u.x),
               mix(dot(hash(i + vec2(0.0,1.0)), f - vec2(0.0,1.0)), 
                   dot(hash(i + vec2(1.0,1.0)), f - vec2(1.0,1.0)), u.x), u.y);
}

float fbm(vec2 uv) {
    float f = 0.0;
    float amp = 0.5;
    float freq = 2.0;
    
    for(int i = 0; i < 5; i++) {
        f += amp * noise(uv * freq);
        uv = mat2(0.8, -0.6, 0.6, 0.8) * uv; 
        amp *= 0.5;
        freq *= 2.0;
    }
    return f * 0.5 + 0.5;
}

void main() {
    vec3 viewDir = normalize(inViewDir);
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    
    // 1. Take the colors directly from C++
    vec3 zenithColor = pc.zenithColor.rgb;
    vec3 horizonColor = pc.horizonColor.rgb;
    
    float horizonMix = clamp(viewDir.y * 4.0, 0.0, 1.0);
    vec3 skyColor = mix(horizonColor, zenithColor, horizonMix);

    // 2. Stars
    if (pc.starFade > 0.0 && viewDir.y > 0.0) {
        // Snap the view vector to a fixed 3D grid so the hash evaluates identically across sub-pixels
        vec3 starGrid = floor(viewDir * 400.0);
        
        // Generate a random number specifically for this grid chunk
        float starHash = fract(sin(dot(starGrid, vec3(12.9898, 78.233, 54.53))) * 43758.5453);
        
        // Only top 0.5% of chunks become a star
        float star = smoothstep(0.995, 1.0, starHash);
        
        // Add a gentle twinkle using the time scale and the star's unique hash
        float twinkle = 0.8 + 0.2 * sin(pc.time * 2.0 + starHash * 100.0);
        
        skyColor += vec3(star * twinkle) * pc.starFade * horizonMix; 
    }
    
    // 3. Sun
    float sunDot = max(dot(viewDir, sunDir), 0.0);
    skyColor += ubo.sunColor.rgb * pow(sunDot, 256.0) * 3.0; 
    skyColor += ubo.sunColor.rgb * pow(sunDot, 8.0) * 0.4;   

    // 4. Moon
    vec3 moonDir = -sunDir;
    float moonDot = max(dot(viewDir, moonDir), 0.0);
    float moonBlend = smoothstep(0.0, -0.2, sunDir.y);
    if (moonBlend > 0.0) {
        vec3 moonColor = vec3(0.55, 0.6, 0.65);
        skyColor += moonColor * pow(moonDot, 512.0) * 2.5 * moonBlend; 
        skyColor += moonColor * pow(moonDot, 16.0) * 0.2 * moonBlend;  
    }

    // 5. Clouds
    if (viewDir.y > 0.01) {
        vec2 cloudUV = viewDir.xz / (viewDir.y + 0.15); 
        cloudUV *= 0.6; 
        
        // Using the safe, pre-scaled CPU time
        vec2 windOffset = vec2(pc.time * 0.015, pc.time * 0.005);
        
        float baseDensity = fbm(cloudUV + windOffset);
        float coverage = pc.coverage; 
        float density = smoothstep(coverage, coverage + 0.25, baseDensity);
        
        if (density > 0.0) {
            // FIX: Recover the exact C++ twilight blend factor (1.0 = Day, 0.0 = Night)
            float twilightBlend = 1.0 - pc.starFade; 
            
            // 1. Calculate the shadow cast by the Sun
            float densityTowardSun = fbm(cloudUV + windOffset + sunDir.xz * 0.15);
            densityTowardSun = smoothstep(coverage, coverage + 0.25, densityTowardSun);
            float sunShadow = clamp(densityTowardSun - density, 0.0, 1.0);
            
            // 2. Calculate the shadow cast by the Moon (Opposite direction)
            float densityTowardMoon = fbm(cloudUV + windOffset - sunDir.xz * 0.15);
            densityTowardMoon = smoothstep(coverage, coverage + 0.25, densityTowardMoon);
            float moonShadow = clamp(densityTowardMoon - density, 0.0, 1.0);
            
            // 3. Smoothly crossfade the shadows during sunset/sunrise! No snapping.
            float shadow = mix(moonShadow, sunShadow, twilightBlend);
            
            // Lighting colors
            vec3 baseCloudWhite = mix(vec3(0.05, 0.05, 0.08), vec3(0.95, 0.98, 1.0), twilightBlend);
            vec3 cloudLight = mix(baseCloudWhite, ubo.sunColor.rgb, 0.6);
            
            float moonBlend = smoothstep(0.0, -0.2, sunDir.y);
            if (moonBlend > 0.0) {
                cloudLight += vec3(0.15, 0.2, 0.25) * moonBlend; 
            }

            vec3 cloudShadow = mix(vec3(0.35, 0.4, 0.45) * twilightBlend, skyColor, 0.4);
            vec3 finalCloudColor = mix(cloudLight, cloudShadow, shadow * 2.0);
            
            float distanceFade = smoothstep(0.01, 0.2, viewDir.y);
            float finalAlpha = clamp(density * distanceFade, 0.0, 1.0);
            
            skyColor = mix(skyColor, finalCloudColor, finalAlpha);
        }
    }

    outColor = vec4(skyColor, 1.0);
}