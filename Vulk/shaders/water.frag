#version 450
#include "common_structures.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec4 outColor;

// Bindless normal textures (binding 3)
layout(binding = 3) uniform sampler2D normalTextures[500];

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    float time;
    uint normalTextureId;
    float tiling;
    float waveStrength;
} pc;

// ------ Helper: compute Fresnel (Schlick approximation) ------
float Fresnel(vec3 viewDir, vec3 normal, float R0) {
    float cosTheta = max(dot(viewDir, normal), 0.0);
    return R0 + (1.0 - R0) * pow(1.0 - cosTheta, 5.0);
}

void main() {
    vec2 uv = inUV;

    // -------- Sample normal map --------
    // Use the normal texture ID from push constants (0 = default if not set)
    vec3 normalMap = texture(normalTextures[pc.normalTextureId], uv).xyz;
    normalMap = normalMap * 2.0 - 1.0;   // from [0,1] to [-1,1]

    // Construct TBN (for simplicity we assume world up = (0,1,0) and the surface is mostly horizontal)
    // A robust method would compute tangent/bitangent from vertex data, but for water we can approximate.
    vec3 N = normalize(inNormal);
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 T = normalize(cross(N, up));
    vec3 B = normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);
    vec3 finalNormal = normalize(TBN * normalMap);

    // -------- Lighting --------
    vec3 viewDir = normalize(ubo.cameraPos - inWorldPos);

    // Diffuse & specular from lights (directional only for simplicity)
    vec3 lightDir = normalize(vec3(1.0, 1.0, 0.0)); // placeholder – use ubo lights if you have them
    vec3 lightColor = vec3(1.0, 1.0, 1.0);
    float diff = max(dot(finalNormal, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(finalNormal, halfDir), 0.0), ubo.specularPower);
    vec3 specular = spec * lightColor * 0.5;

    // Ambient
    vec3 ambient = vec3(ubo.ambient);

    // -------- Fresnel (reflectivity) --------
    float R0 = 0.02;   // base reflectivity for water
    float fresnel = Fresnel(viewDir, finalNormal, R0);

    // -------- Base water color (deep blue/green) --------
    vec3 waterColor = vec3(0.1, 0.35, 0.5);

    // Combine lighting (simple Blinn‑Phong)
    vec3 lightResult = ambient + diffuse + specular;
    vec3 finalColor = waterColor * lightResult;

    // Add Fresnel highlight (mix with sky/reflection later)
    finalColor = mix(finalColor, vec3(1.0), fresnel * 0.3);

    // -------- Fog (from UBO) --------
    float dist = length(inWorldPos - ubo.cameraPos);
    float fogFactor = clamp((dist - ubo.fogStart) / (ubo.fogEnd - ubo.fogStart), 0.0, 1.0);
    vec3 fogColor = vec3(0.5, 0.6, 0.7); // or use skybox color
    finalColor = mix(finalColor, fogColor, fogFactor);

    // -------- Alpha (translucency) --------
    float alpha = 0.75;   // base opacity – you can make it depth‑dependent later

    outColor = vec4(finalColor, alpha);
}