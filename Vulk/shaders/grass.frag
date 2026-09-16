#version 450
#include "common_structures.glsl"

struct Light {
    vec4 positionOrDir;
    vec4 color;
    vec4 params;
};

layout(std430, set = 0, binding = 1) readonly buffer LightBuffer {
    Light lights[];
} lightBuffer;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    float time; uint textureId; float windStrength; float windSpeed; float lodFactor;
} pc;

void main() {
    vec3 bottomColor = vec3(0.02, 0.15, 0.03); 
    vec3 topColor    = vec3(0.35, 0.65, 0.15); 
    
    float gradient = pow(fragUV.y, 0.8);
    vec3 baseColor = mix(topColor, bottomColor, gradient);
    
    vec3 N = normalize(fragNormal);
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    float sunDiffuse = max(dot(N, sunDir), 0.0); 
    
    float wrappedSun = sunDiffuse * 0.8 + 0.2;
    vec3 accumulatedLight = (ubo.sunColor.rgb * ubo.sunColor.a * wrappedSun) + vec3(ubo.ambient * 0.9);

    for (uint i = 0; i < ubo.lightCount; ++i) {
        Light l = lightBuffer.lights[i];
        
        if (l.positionOrDir.w > 0.5) { 
            vec3 toLight = l.positionOrDir.xyz - fragWorldPos;
            float distSq = dot(toLight, toLight);
            float range = max(l.params.x, 0.001);
            
            if (distSq < range * range) {
                float dist = sqrt(distSq);
                vec3 L = toLight / dist;
                
                float nDotL = dot(N, L);
                float wrappedPoint = max(nDotL, 0.0) * 0.7 + max(-nDotL, 0.0) * 0.4;
                
                // FIX: Synchronized Attenuation Math with Terrain!
                float atten = pow(max(1.0 - (dist / range), 0.0), 2.0);

                accumulatedLight += l.color.rgb * l.color.a * wrappedPoint * atten;
            }
        }
    }

    outColor = vec4(baseColor * accumulatedLight, 1.0);
}