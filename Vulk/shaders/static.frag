#version 450
#extension GL_EXT_nonuniform_qualifier : require
#include "common_structures.glsl"

struct Light {
    vec4 positionOrDir;
    vec4 color;
    vec4 params;
};

layout(std430, binding = 1) readonly buffer LightBuffer {
    Light lights[];
} lightBuffer;

layout(binding = 2) uniform sampler2D globalTextures[];
layout(binding = 3) uniform sampler2D normalTextures[];

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint fragTextureId;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in float fragTangentHandedness;
layout(location = 6) flat in uint fragNormalTextureId;

layout(location = 0) out vec4 outColor;

vec3 UnpackNormal(vec4 sampledNormal) {
    vec2 normalXY = sampledNormal.rg * 2.0 - 1.0;
    float normalZ = sqrt(max(0.0, 1.0 - dot(normalXY, normalXY)));
    return vec3(normalXY, normalZ);
}

vec3 CalcBlinnPhong(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, float specPower) {
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), specPower);
    return diff * lightColor * albedo + spec * lightColor * 0.1;
}

void main() {
    // 1. Texture Lookup
    vec4 albedo = texture(globalTextures[nonuniformEXT(fragTextureId)], fragTexCoord);
    
    // 2. Alpha Clip (Increased to 0.5 to cleanly cut out billboard backgrounds)
    if (albedo.a < 0.5) {
        discard;
    }

    float ditherNoise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float distToCam = length(ubo.cameraPos - fragWorldPos);

    float maxFadeDistance = ubo.fadeParams.w; 
    float fadeStartDistance = ubo.fadeParams.z; 
    
    float fadeAlpha = 1.0 - clamp((distToCam - fadeStartDistance) / (maxFadeDistance - fadeStartDistance), 0.0, 1.0);
    
    if (ditherNoise > fadeAlpha) {
        discard;
    }

    // 4. Tangent Space normal mapping setup
    vec3 N_geo = normalize(fragNormal);
    vec3 T = normalize(fragTangent);
    T = normalize(T - N_geo * dot(N_geo, T)); 
    vec3 B = cross(N_geo, T) * fragTangentHandedness;
    mat3 TBN = mat3(T, B, N_geo);

    vec4 sampledNormal = texture(normalTextures[nonuniformEXT(fragNormalTextureId)], fragTexCoord);
    vec3 normalMap = UnpackNormal(sampledNormal);
    vec3 N = normalize(TBN * normalMap);

    // 5. Lighting
    vec3 V = normalize(ubo.cameraPos - fragWorldPos);
    vec3 result = ubo.ambient * albedo.xyz;

    for (uint i = 0u; i < ubo.lightCount; ++i) {
        Light L = lightBuffer.lights[i];
        vec3 lightVec = normalize(L.positionOrDir.xyz - (L.positionOrDir.w > 0.5 ? fragWorldPos : vec3(0.0)));
        float atten = (L.positionOrDir.w > 0.5) ? (1.0 / length(L.positionOrDir.xyz - fragWorldPos)) : 1.0;
        
        result += CalcBlinnPhong(N, V, lightVec, L.color.rgb * L.color.a * atten, albedo.xyz, ubo.specularPower);
    }

    // 6. Fog
    float fogFactor = clamp((distToCam - ubo.fogStart) / (ubo.fogEnd - ubo.fogStart), 0.0, 1.0);
    if (distToCam > ubo.fogEnd) fogFactor = 1.0;
    
    vec3 fogColor = vec3(0.6, 0.7, 0.8);
    vec3 finalColor = mix(result, fogColor, fogFactor);

    outColor = vec4(finalColor, 1.0);
}