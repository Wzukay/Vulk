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

    vec3 cleanSkyGradient = mix(horizonColor, zenithColor, horizonMix);

    vec3 skyColor = cleanSkyGradient;

    // 2. Stars (Locked to grid, gentle twinkle)
    if (pc.starFade > 0.0 && viewDir.y > 0.0) {
        vec3 starGrid = floor(viewDir * 400.0);
        float starHash = fract(sin(dot(starGrid, vec3(12.9898, 78.233, 54.53))) * 43758.5453);
        float star = smoothstep(0.995, 1.0, starHash);
        float twinkle = 0.8 + 0.2 * sin(pc.time * 2.0 + starHash * 100.0);
        
        skyColor += vec3(star * twinkle) * pc.starFade * horizonMix; 
    }
    
    // 3. Sun
    float sunDot = max(dot(viewDir, sunDir), 0.0);
    float sunIntensity = pow(sunDot, 2048.0) * 3.0 + pow(sunDot, 32.0) * 0.4;
    skyColor += ubo.sunColor.rgb * sunIntensity;

    // 4. Moon
    vec3 moonDir = -sunDir;
    float moonDot = max(dot(viewDir, moonDir), 0.0);
    float moonBlend = smoothstep(0.0, -0.2, sunDir.y);
    float moonIntensity = 0.0;

    if (moonBlend > 0.0) {
        vec3 moonColor = vec3(0.55, 0.6, 0.65);
        moonIntensity = (pow(moonDot, 512.0) * 2.5 + pow(moonDot, 16.0) * 0.2) * moonBlend;
        skyColor += moonColor * moonIntensity; 
    }

    // 5. Fast 2.5D Clouds
    if (viewDir.y > 0.01) {
        float twilightBlend = 1.0 - pc.starFade; 
        vec2 windOffset = vec2(pc.time * 0.015, pc.time * 0.005);
        float coverage = pc.coverage; 
        
        // LAYER 1: Cloud Bottoms
        vec2 uvBottom = viewDir.xz / (viewDir.y + 0.15); 
        uvBottom *= 0.6; 
        float baseBottom = fbm(uvBottom + windOffset);
        float densityBottom = smoothstep(coverage, coverage + 0.20, baseBottom);
        
        // LAYER 2: Cloud Tops
        // Pushed height up to 0.25 for much stronger 3D parallax when moving the camera
        vec2 uvTop = viewDir.xz / (viewDir.y + 0.25); 
        uvTop *= 0.6;
        float baseTop = fbm(uvTop + windOffset);
        float densityTop = smoothstep(coverage, coverage + 0.20, baseTop);

        float finalDensity = max(densityBottom, densityTop);

        if (finalDensity > 0.0) {
            // Directional Shadows (Sun/Moon blocking)
            float densityTowardSun = fbm(uvBottom + windOffset + sunDir.xz * 0.12);
            densityTowardSun = smoothstep(coverage, coverage + 0.30, densityTowardSun);
            float sunShadow = clamp(densityTowardSun - densityBottom, 0.0, 1.0);
            
            float densityTowardMoon = fbm(uvBottom + windOffset - sunDir.xz * 0.12);
            densityTowardMoon = smoothstep(coverage, coverage + 0.30, densityTowardMoon);
            float moonShadow = clamp(densityTowardMoon - densityBottom, 0.0, 1.0);
            
            float directionalShadow = mix(moonShadow, sunShadow, twilightBlend);
            
            // NEW: Pillowy Volume Math
            // Calculates a fake "Up" normal by comparing the top and bottom layer densities.
            // 1.0 = Peak of the cloud, 0.0 = Deep underbelly
            float fakeNormalY = smoothstep(0.0, 0.6, densityTop - (densityBottom * 0.5));
            
            vec3 baseCloudWhite = mix(vec3(0.05, 0.05, 0.08), vec3(0.95, 0.98, 1.0), twilightBlend);
            vec3 cloudLight = mix(baseCloudWhite, ubo.sunColor.rgb, 0.6);
            if (moonBlend > 0.0) {
                cloudLight += vec3(0.15, 0.2, 0.25) * moonBlend; 
            }

            vec3 cloudShadow = mix(vec3(0.35, 0.4, 0.45) * twilightBlend, cleanSkyGradient, 0.4);
            
            // 1. Apply directional shadow (sun/moon occlusion)
            vec3 finalCloudColor = mix(cloudLight, cloudShadow, directionalShadow * 1.5);
            
            // 2. Apply vertical volume shadow (underbellies get dark, tops stay bright)
            // This forces the bottom layer to look like a thick volumetric shadow
            finalCloudColor = mix(cloudShadow, finalCloudColor, fakeNormalY * 0.8 + 0.2);
            
            float distanceFade = smoothstep(0.01, 0.2, viewDir.y);
            float finalAlpha = clamp(finalDensity * distanceFade, 0.0, 1.0);
            
            skyColor = mix(skyColor, finalCloudColor, finalAlpha);
        }
    }

    outColor = vec4(skyColor, 0.0);
}