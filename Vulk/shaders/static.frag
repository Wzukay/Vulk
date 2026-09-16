#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(early_fragment_tests) in;

#include "common_structures.glsl"

struct Light {
    vec4 positionOrDir;
    vec4 color;
    vec4 params;
};

struct ClusterRecord { uint offset; uint count; };

layout(std430, set = 0, binding = 1) readonly buffer LightBuffer { Light lights[]; } lightBuffer;
layout(set = 0, binding = 2) uniform sampler2D globalTextures[];
layout(set = 0, binding = 3) uniform sampler2D normalTextures[];

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

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;
const float ALPHA_CUTOFF = 0.5;

vec3 ApplyFog(vec3 color, float distanceToCamera) {
    float fogRange = max(ubo.fogEnd - ubo.fogStart, 0.001);
    float fogFactor = clamp((distanceToCamera - ubo.fogStart) / fogRange, 0.0, 1.0);
    return mix(color, vec3(0.6, 0.7, 0.8), fogFactor);
}

vec3 UnpackNormal(vec4 sampledNormal) {
    vec2 normalXY = sampledNormal.rg * 2.0 - 1.0;
    float normalZ = sqrt(max(0.0, 1.0 - dot(normalXY, normalXY)));
    return vec3(normalXY, normalZ);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denominator = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 0.0000001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, L), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(N, V), 0.0), roughness);
}

vec3 CalcPBR(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, float roughness, float metallic) {
    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 specular = DistributionGGX(N, H, roughness) * GeometrySmith(N, V, L, roughness) * F /
        max(4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001, 0.0001);

    vec3 diffuse = (vec3(1.0) - F) * (1.0 - metallic) * albedo / PI;

    return (diffuse + specular) * lightColor * max(dot(N, L), 0.0);
}

void main() {
    vec4 albedo = texture(globalTextures[nonuniformEXT(fragTextureId)], fragTexCoord);

    if (albedo.a < ALPHA_CUTOFF) { discard; }

    float distanceToCamera = length(ubo.cameraPos - fragWorldPos);
    float ditherNoise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float fadeRange = max(ubo.fadeParams.y - ubo.fadeParams.x, 0.001);
    float fadeFactor = clamp((distanceToCamera - ubo.fadeParams.x) / fadeRange, 0.0, 1.0);

    if (ditherNoise > 1.0 - fadeFactor) { discard; }

    vec3 geometricNormal = normalize(fragNormal);
    float cheapStart = max(150.0, ubo.fadeParams.x * 0.5);
    float cheapEnd = min(ubo.fadeParams.x, cheapStart + 60.0);
    float cheapRange = max(cheapEnd - cheapStart, 0.001);
    float cheapBlend = clamp((distanceToCamera - cheapStart) / cheapRange, 0.0, 1.0);

    // Cheap LOD shading (Early exit, ignores point lights)
    if (ditherNoise < cheapBlend) {
        vec3 sunDirection = normalize(ubo.sunDirection.xyz);
        float sunDiffuse = max(dot(geometricNormal, sunDirection), 0.0);
        vec3 cheapLighting = vec3(ubo.ambient * 0.8) + ubo.sunColor.rgb * ubo.sunColor.a * sunDiffuse;
        outColor = vec4(ApplyFog(albedo.rgb * cheapLighting, distanceToCamera), albedo.a);
        return;
    }

    vec3 tangent = normalize(fragTangent);
    tangent = normalize(tangent - geometricNormal * dot(geometricNormal, tangent));
    mat3 TBN = mat3(tangent, cross(geometricNormal, tangent) * fragTangentHandedness, geometricNormal);
    vec3 N = normalize(TBN * UnpackNormal(texture(normalTextures[nonuniformEXT(fragNormalTextureId)], fragTexCoord)));

    float ao = 1.0;           
    float roughness = 0.5;    
    float metallic = 0.0;     
    vec3 V = normalize(ubo.cameraPos - fragWorldPos);
    vec3 result = vec3(0.0);

    // --- CLUSTERED FORWARD LOOKUP ---
    float viewZ = -(ubo.view * vec4(fragWorldPos, 1.0)).z; 
    // FIX: Using ubo._pad2.x for renderDistance
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
        vec3 L;
        float attenuation = 1.0;

        if (isPointLight) {
            vec3 toLight = light.positionOrDir.xyz - fragWorldPos;
            float distanceSquared = dot(toLight, toLight);
            float range = max(light.params.x, 0.001);
            float rangeSquared = range * range;

            if (distanceSquared >= rangeSquared) continue;

            L = toLight * inversesqrt(max(distanceSquared, 0.0001));
            float normalizedDistance = sqrt(distanceSquared) / range;
            float rangeFade = max(1.0 - normalizedDistance * normalizedDistance, 0.0);
            attenuation = (rangeFade * rangeFade) / max(distanceSquared, 1.0);
        } else {
            L = normalize(light.positionOrDir.xyz);
        }

        float nDotL = dot(N, L);
        // Foliage transmission: allow light through the back of leaves
        float foliageFactor = max(nDotL, 0.0) + max(-nDotL, 0.0) * 0.4;
        if (foliageFactor <= 0.001) continue;

        vec3 radiance = light.color.rgb * light.color.a * attenuation;
        
        if (nDotL > 0.0) {
            result += CalcPBR(N, V, L, radiance, albedo.rgb, roughness, metallic);
        } else {
            result += (albedo.rgb / PI) * radiance * (max(-nDotL, 0.0) * 0.4);
        }
    }

    vec3 skyColor = vec3(0.2, 0.3, 0.45);
    vec3 groundColor = vec3(0.05, 0.04, 0.03);

    result += mix(groundColor, skyColor, N.y * 0.5 + 0.5) * albedo.rgb * ao * ubo.ambient;

    outColor = vec4(ApplyFog(result, distanceToCamera), albedo.a);
}