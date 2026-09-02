#version 450
#extension GL_EXT_nonuniform_qualifier : require
#include "common_structures.glsl"

struct Light { vec4 positionOrDir; vec4 color; vec4 params; };

layout(std430, binding = 1) readonly buffer LightBuffer { Light lights[]; } lightBuffer;

layout(binding = 2) uniform sampler2D globalTextures[];
layout(binding = 3) uniform sampler2D normalTextures[];
layout(binding = 5) uniform sampler2D ormTextures[];

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint fragTextureId;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in float fragTangentHandedness;
layout(location = 6) flat in uint fragNormalTextureId;
layout(location = 7) flat in uint fragOrmTextureId;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

vec3 UnpackNormal(vec4 sampledNormal) {
    vec2 normalXY = sampledNormal.rg * 2.0 - 1.0;
    float normalZ = sqrt(max(0.0, 1.0 - dot(normalXY, normalXY)));
    return vec3(normalXY, normalZ);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) { return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0); }

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 0.0000001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, L), 0.0), roughness) * GeometrySchlickGGX(max(dot(N, V), 0.0), roughness);
}

vec3 CalcPBR(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, float roughness, float metallic) {
    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);       
    vec3 specular = (DistributionGGX(N, H, roughness) * GeometrySmith(N, V, L, roughness) * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic); 
    return (kD * albedo / PI + specular) * lightColor * max(dot(N, L), 0.0);
}

void main() {
    vec4 albedo = texture(globalTextures[nonuniformEXT(fragTextureId)], fragTexCoord);
    if (albedo.a < 0.5) discard;

    float ditherNoise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float distToCam = length(ubo.cameraPos - fragWorldPos);
    if (ditherNoise > (1.0 - clamp((distToCam - ubo.fadeParams.x) / (ubo.fadeParams.y - ubo.fadeParams.x), 0.0, 1.0))) discard;

    vec3 N_geo = normalize(fragNormal);
    vec3 T = normalize(fragTangent);
    T = normalize(T - N_geo * dot(N_geo, T)); 
    mat3 TBN = mat3(T, cross(N_geo, T) * fragTangentHandedness, N_geo);

    vec3 N = normalize(TBN * UnpackNormal(texture(normalTextures[nonuniformEXT(fragNormalTextureId)], fragTexCoord)));

    // Extract PBR parameters
    vec4 orm = texture(ormTextures[nonuniformEXT(fragOrmTextureId)], fragTexCoord);
    float ao = orm.r;
    float roughness = orm.g;
    float metallic = orm.b;

    vec3 V = normalize(ubo.cameraPos - fragWorldPos);
    vec3 result = vec3(0.0);

    for (uint i = 0u; i < ubo.lightCount; ++i) {
        Light L = lightBuffer.lights[i];
        vec3 lightVec = normalize(L.positionOrDir.xyz - (L.positionOrDir.w > 0.5 ? fragWorldPos : vec3(0.0)));
        float atten = (L.positionOrDir.w > 0.5) ? (1.0 / length(L.positionOrDir.xyz - fragWorldPos)) : 1.0;
        result += CalcPBR(N, V, lightVec, L.color.rgb * L.color.a * atten, albedo.xyz, roughness, metallic);
    }

    vec3 skyColor = vec3(0.2, 0.3, 0.45);
    vec3 groundColor = vec3(0.05, 0.04, 0.03);
    result += mix(groundColor, skyColor, N.y * 0.5 + 0.5) * albedo.xyz * ao * ubo.ambient; 

    float fogFactor = clamp((distToCam - ubo.fogStart) / (ubo.fogEnd - ubo.fogStart), 0.0, 1.0);
    if (distToCam > ubo.fogEnd) fogFactor = 1.0;
    outColor = vec4(mix(result, vec3(0.6, 0.7, 0.8), fogFactor), 1.0);
}