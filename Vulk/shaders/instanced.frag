#version 450
#extension GL_EXT_nonuniform_qualifier : require

#include "common_structures.glsl"

struct Light {
    vec4 positionOrDir;
    vec4 color;
    vec4 params;
};
struct ClusterRecord { uint offset; uint count; };

// --- BINDINGS ---
layout(std430, set = 0, binding = 1) readonly buffer LightBuffer { Light lights[]; } lightBuffer;
layout(set = 0, binding = 2) uniform sampler2D globalTextures[];

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
layout(location = 7) in float fragOpacity;

layout(location = 0) out vec4 outColor;

const float ALPHA_CUTOFF = 0.5;

vec3 ApplyFog(vec3 color, float distanceToCamera) {
    float fogRange = max(ubo.fogEnd - ubo.fogStart, 0.001);
    float fogFactor = clamp((distanceToCamera - ubo.fogStart) / fogRange, 0.0, 1.0);
    return mix(color, vec3(0.6, 0.7, 0.8), fogFactor);
}

void main() {
    float actualOpacity = abs(fragOpacity);

    if (actualOpacity < 0.99) {
        int x = int(mod(gl_FragCoord.x, 4.0));
        int y = int(mod(gl_FragCoord.y, 4.0));
        
        const float bayer[16] = float[](
             0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
            12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
             3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
            15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
        );
        
        float ditherThreshold = bayer[y * 4 + x];
        
        if (fragOpacity >= 0.0) {
            if (actualOpacity < ditherThreshold) discard;
        } else {
            if (ditherThreshold <= 1.0 - actualOpacity) discard;
        }
    }

    vec4 albedo = texture(globalTextures[nonuniformEXT(fragTextureId)], fragTexCoord);

    if (albedo.a < ALPHA_CUTOFF) discard;

    float distanceToCamera = length(ubo.cameraPos - fragWorldPos);
    float fadeRange = max(ubo.fadeParams.y - ubo.fadeParams.x, 0.001);
    float fadeFactor = clamp((distanceToCamera - ubo.fadeParams.x) / fadeRange, 0.0, 1.0);
    float ditherNoise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));

    if (ditherNoise > 1.0 - fadeFactor) discard;

    vec3 normal = normalize(fragNormal);
    vec3 sunDirection = normalize(ubo.sunDirection.xyz);
    float sunDiffuse = max(dot(normal, sunDirection), 0.0);
    
    // A small wrap term keeps the underside of foliage readable.
    float wrappedSun = sunDiffuse * 0.85 + 0.15;

    // Start with Ambient + Sun
    vec3 lighting = vec3(ubo.ambient * 0.9) + ubo.sunColor.rgb * ubo.sunColor.a * wrappedSun;

    // --- NEW: CLUSTERED FORWARD LOOKUP FOR POINT LIGHTS ---
    float viewZ = -(ubo.view * vec4(fragWorldPos, 1.0)).z; 
    float slice = log2(max(viewZ, 2.0) / 2.0) * (24.0 / log2(ubo._pad2.x / 2.0));
    uint clusterZ = clamp(uint(slice), 0u, 23u);
    uint clusterX = clamp(uint(gl_FragCoord.x / (ubo.screenSize.x / 16.0)), 0u, 15u);
    uint clusterY = clamp(uint(gl_FragCoord.y / (ubo.screenSize.y / 9.0)), 0u, 8u);
    uint clusterIdx = clusterX + (clusterY * 16u) + (clusterZ * 16u * 9u);

    ClusterRecord record = grid[clusterIdx];

    for (uint i = 0u; i < record.count; ++i) {
        uint lightIdx = indices[record.offset + i];
        Light light = lightBuffer.lights[lightIdx];

        bool isPointLight = light.positionOrDir.w > 0.5;
        if (!isPointLight) continue;

        vec3 toLight = light.positionOrDir.xyz - fragWorldPos;
        float distanceSquared = dot(toLight, toLight);
        float range = max(light.params.x, 0.001);
        
        if (distanceSquared >= range * range) continue;
        
        vec3 L = toLight * inversesqrt(max(distanceSquared, 0.0001));

        // FIX: Synchronized Attenuation Math with Terrain!
        float attenuation = pow(max(1.0 - (sqrt(distanceSquared) / range), 0.0), 2.0);

        float nDotL = dot(normal, L);
        float wrappedPointLight = max(nDotL, 0.0) * 0.85 + max(-nDotL, 0.0) * 0.4 + 0.05;

        lighting += light.color.rgb * light.color.a * wrappedPointLight * attenuation;
    }

    outColor = vec4(ApplyFog(albedo.rgb * lighting, distanceToCamera), albedo.a);

    if (fragNormalTextureId == 999999) {
        outColor.a += fragTangentHandedness;
    }
}