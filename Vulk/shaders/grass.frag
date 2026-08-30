#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#include "common_structures.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in float inBladeRand;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 2) uniform sampler2D textureSamplers[128];

layout(push_constant) uniform PushConstants {
    float time;
    uint textureId;
    float windStrength;
    float windSpeed;
    float lodFactor;
} push;

void main() {
    vec4 texColor = texture(textureSamplers[push.textureId], inUV);

    // 1. Base Cutout
    if (texColor.a < 0.3 || length(texColor.rgb) < 0.15) discard;

    // 2. Screen-Space Dithered LOD Dissolve
    float ditherNoise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float distToCam = length(ubo.cameraPos - inWorldPos);

    float maxFadeDistance = ubo.fadeParams.w; 
    float fadeStartDistance = ubo.fadeParams.z; 
    
    float fadeAlpha = 1.0 - clamp((distToCam - fadeStartDistance) / (maxFadeDistance - fadeStartDistance), 0.0, 1.0);
    
    if (ditherNoise > fadeAlpha) {
        discard;
    }

    // 3. Dynamic Day/Night Lighting
    vec3 N = normalize(inNormal);
    vec3 lightDir = normalize(ubo.sunDirection.xyz); // <-- Now tracks the sun/moon!
    
    // N dot L for smooth shading
    float diff = max(0.0, dot(N, lightDir));
    
    // Apply true UBO ambient and true Sun/Moon color
    vec3 ambientLight = vec3(ubo.ambient);
    vec3 directionalLight = ubo.sunColor.rgb * ubo.sunColor.a * diff;
    
    vec3 litColor = texColor.rgb * (ambientLight + directionalLight);

    // 4. Dynamic Time-of-Day Fog (STYLIZED)
    float fogFactor = clamp((distToCam - ubo.fogStart) / (ubo.fogEnd - ubo.fogStart), 0.0, 1.0);
    if (distToCam > ubo.fogEnd) fogFactor = 1.0;
    
    vec3 nH  = vec3(0.005, 0.01, 0.02);
    vec3 twH = vec3(0.45, 0.10, 0.25);
    vec3 ssH = vec3(1.00, 0.45, 0.10);
    vec3 dH  = vec3(0.50, 0.75, 0.95);
    
    float t = ubo.sunDirection.y;
    float b1 = smoothstep(-0.20, -0.05, t);
    float b2 = smoothstep(-0.05,  0.08, t);
    float b3 = smoothstep( 0.08,  0.35, t);
    
    vec3 fogColor = mix(mix(mix(nH, twH, b1), ssH, b2), dH, b3);
    
    vec3 finalColor = mix(litColor, fogColor, fogFactor);

    outColor = vec4(finalColor, 1.0);
}