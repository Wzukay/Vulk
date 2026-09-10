#version 450

#include "common_structures.glsl"

struct Light { vec4 positionOrDir; vec4 color; vec4 params; };
struct ClusterRecord { uint offset; uint count; };

layout(std430, set = 0, binding = 1) readonly buffer LightBuffer { Light lights[]; } lightBuffer;

// --- CLUSTER DATA (SET 1) ---
layout(std430, set = 1, binding = 0) readonly buffer GridBuffer { ClusterRecord grid[]; };
layout(std430, set = 1, binding = 1) readonly buffer IndexBuffer { uint globalIndexCount; uint indices[]; };

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

vec3 valueNoiseGrad(vec2 x) {
    vec2 i = floor(x);
    vec2 f = fract(x);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0); 

    float a = hash(i); float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0)); float d = hash(i + vec2(1.0, 1.0));

    float k0 = a; float k1 = b - a; float k2 = c - a; float k3 = a - b - c + d;
    float h = k0 + k1 * u.x + k2 * u.y + k3 * u.x * u.y;
    vec2 grad = du * vec2(k1 + k3 * u.y, k2 + k3 * u.x);

    return vec3(h, grad.x, grad.y);
}

void main() {
    float distanceToCamera = length(ubo.cameraPos - fragWorldPos);
    vec3 normal = normalize(fragNormal);

    if (fragTangentHandedness < -0.5) {
        vec3 viewDir = normalize(ubo.cameraPos - fragWorldPos);
        vec3 sunDir = normalize(ubo.sunDirection.xyz);
        
        float ripple = valueNoiseGrad(fragWorldPos.xz * 0.8).x;
        vec3 waterNormal = normalize(vec3(ripple * 0.1, 1.0, ripple * 0.1));
        
        vec3 deepWater = vec3(0.02, 0.15, 0.25);
        vec3 finalWaterColor = mix(deepWater, fragColor, 0.3 + ripple * 0.2);
        
        vec3 reflectDir = reflect(-sunDir, waterNormal);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 128.0) * 1.5;
        
        vec3 lighting = vec3(ubo.ambient) + ubo.sunColor.rgb * (max(dot(waterNormal, sunDir), 0.0) + spec);
        outColor = vec4(ApplyFog(finalWaterColor * lighting, distanceToCamera), 1.0);
        return; 
    }

    vec3 blendWeights = max(fragTangent, vec3(0.0));
    float weightSum = blendWeights.r + blendWeights.g + blendWeights.b;
    blendWeights /= max(weightSum, 0.0001);
    
    float n = valueNoiseGrad(fragWorldPos.xz * 0.02).x;
    float slope = clamp(normal.y, 0.0, 1.0);
    float elevation = clamp(fragWorldPos.y / 150.0, 0.0, 1.0);
    float noisyElevation = clamp(elevation + (n * 0.3 - 0.15), 0.0, 1.0);

    vec3 terrainColor = fragColor;
    float colorBreakup = mix(0.85, 1.15, n);
    float cliffDarken = mix(1.0, 0.6, blendWeights.b);
    terrainColor *= colorBreakup * cliffDarken;

    float snowMask = smoothstep(0.7, 0.9, noisyElevation) * smoothstep(0.6, 0.9, slope);
    terrainColor = mix(terrainColor, vec3(0.85, 0.88, 0.92), snowMask);

    // --- CLUSTERED FORWARD LIGHTING ---
    float viewZ = -(ubo.view * vec4(fragWorldPos, 1.0)).z; 
    // FIX: Using ubo._pad2.x for renderDistance
    float slice = log2(max(viewZ, 2.0) / 2.0) * (24.0 / log2(ubo._pad2.x / 2.0));
    uint clusterZ = clamp(uint(slice), 0u, 23u);
    uint clusterX = clamp(uint(gl_FragCoord.x / (ubo.screenSize.x / 16.0)), 0u, 15u);
    uint clusterY = clamp(uint(gl_FragCoord.y / (ubo.screenSize.y / 9.0)), 0u, 8u);
    uint clusterIdx = clusterX + (clusterY * 16u) + (clusterZ * 16u * 9u);

    ClusterRecord record = grid[clusterIdx];
    vec3 lighting = vec3(ubo.ambient);

    for (uint i = 0u; i < record.count; ++i) {
        uint lightIdx = indices[record.offset + i];
        Light light = lightBuffer.lights[lightIdx];

        bool isPointLight = light.positionOrDir.w > 0.5;
        if (isPointLight) {
            vec3 toLight = light.positionOrDir.xyz - fragWorldPos;
            float distanceSquared = dot(toLight, toLight);
            float range = max(light.params.x, 0.001);
            if (distanceSquared >= range * range) continue;
            
            vec3 L = toLight * inversesqrt(max(distanceSquared, 0.0001));
            float attenuation = pow(max(1.0 - (sqrt(distanceSquared) / range), 0.0), 2.0);
            
            float diff = max(dot(normal, L), 0.0) * 0.85 + 0.15;
            lighting += light.color.rgb * light.color.a * diff * attenuation;
        } else {
            vec3 L = normalize(light.positionOrDir.xyz);
            float diff = max(dot(normal, L), 0.0) * 0.85 + 0.15;
            lighting += light.color.rgb * light.color.a * diff;
        }
    }

    float terrainOcclusion = mix(0.72, 1.0, slope);
    vec3 finalColor = terrainColor * lighting * terrainOcclusion;
    
    outColor = vec4(ApplyFog(finalColor, distanceToCamera), 1.0);
}