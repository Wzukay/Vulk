#version 450
#include "common_structures.glsl"

layout(location = 0) in vec3 inViewDir;
layout(location = 0) out vec4 outColor;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

void main() {
    vec3 viewDir = normalize(inViewDir);
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    
    float height = max(0.0, viewDir.y); 
    
    // --- 1. STYLIZED 4-PHASE COLOR PALETTES ---
    vec3 nZ  = vec3(0.002, 0.002, 0.008); 
    vec3 nH  = vec3(0.005, 0.01, 0.02);
    
    vec3 twZ = vec3(0.05, 0.04, 0.14);   // Indigo Twilight Zenith
    vec3 twH = vec3(0.45, 0.10, 0.25);   // Magenta Twilight Horizon
    
    vec3 ssZ = vec3(0.10, 0.25, 0.50);   // Blue Sunrise Zenith
    vec3 ssH = vec3(1.00, 0.45, 0.10);   // Vibrant Orange Sunrise Horizon
    
    vec3 dZ  = vec3(0.05, 0.35, 0.75);   // Azure Day Zenith
    vec3 dH  = vec3(0.50, 0.75, 0.95);   // Soft Light Blue Day Horizon
    
    // Smooth S-Curve blends based on sun height
    float t = sunDir.y;
    float b1 = smoothstep(-0.20, -0.05, t); // Night -> Twilight
    float b2 = smoothstep(-0.05,  0.08, t); // Twilight -> Sunrise
    float b3 = smoothstep( 0.08,  0.35, t); // Sunrise -> Day
    
    vec3 curZ = mix(mix(mix(nZ, twZ, b1), ssZ, b2), dZ, b3);
    vec3 curH = mix(mix(mix(nH, twH, b1), ssH, b2), dH, b3);
    
    // Smooth gradient from horizon to zenith
    vec3 skyColor = mix(curH, curZ, pow(height, 0.5));
    
    // --- 2. STYLIZED SUN ---
    float sunDot = dot(viewDir, sunDir);
    // Larger, slightly softer sun disk
    float sunDisk = smoothstep(0.998, 0.9995, sunDot); 
    
    // Rich, painted atmospheric glow
    float sunGlow = pow(max(0.0, sunDot), 12.0) * 0.4 + pow(max(0.0, sunDot), 64.0) * 0.3;
    vec3 glowColor = mix(mix(vec3(0.8, 0.1, 0.5), vec3(1.0, 0.4, 0.1), b2), vec3(1.0, 0.9, 0.7), b3);
    
    // --- 3. MAGICAL MOON ---
    vec3 moonDir = -sunDir; 
    float moonDot = dot(viewDir, moonDir);
    float moonDisk = smoothstep(0.9985, 0.9995, moonDot);
    float moonGlow = pow(max(0.0, moonDot), 64.0) * 0.05;
    vec3 moonColor = vec3(0.65, 0.7, 0.75);
    
    // --- 4. STARS ---
    float starField = 0.0;
    if (b3 < 0.8 && viewDir.y > 0.0) {
        float starRand = hash(floor(viewDir * 450.0)); 
        starField = smoothstep(0.996, 1.0, starRand) * (1.0 - b3);
        starField *= smoothstep(0.99, 0.96, moonDot); // Dim stars around the moon
    }
    
    // --- 5. FOG & GROUND BLEND ---
    if (viewDir.y < 0.0) {
        skyColor = mix(curH, curH * 0.2, clamp(-viewDir.y * 5.0, 0.0, 1.0));
    }
    
    vec3 finalColor = skyColor 
                    + (sunDisk * ubo.sunColor.rgb * 1.5) 
                    + (sunGlow * glowColor * max(0.0, sunDir.y + 0.15))
                    // Lowered the moon disk multiplier to 1.2
                    + (moonDisk * moonColor * 1.2)
                    + (moonGlow * moonColor)
                    + vec3(max(0.0, starField));
    
    outColor = vec4(finalColor, 1.0);
}