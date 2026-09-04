#version 450
#extension GL_EXT_nonuniform_qualifier : require

#include "common_structures.glsl"

layout(binding = 2) uniform sampler2D globalTextures[];

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint fragTextureId;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 4) in vec3 fragTangent;
layout(location = 5) in float fragTangentHandedness;
layout(location = 6) flat in uint fragNormalTextureId;

layout(location = 0) out vec4 outColor;

const float ALPHA_CUTOFF = 0.5;

vec3 ApplyFog(vec3 color, float distanceToCamera) {
    float fogRange = max(ubo.fogEnd - ubo.fogStart, 0.001);

    float fogFactor = clamp(
        (distanceToCamera - ubo.fogStart) / fogRange,
        0.0,
        1.0);

    return mix(color, vec3(0.6, 0.7, 0.8), fogFactor);
}

void main() {
    vec4 albedo = texture(
        globalTextures[nonuniformEXT(fragTextureId)],
        fragTexCoord);

    // Leaf-card holes must not write depth or pay for lighting.
    if (albedo.a < ALPHA_CUTOFF) {
        discard;
    }

    float distanceToCamera =
        length(ubo.cameraPos - fragWorldPos);

    float fadeRange =
        max(ubo.fadeParams.y - ubo.fadeParams.x, 0.001);

    float fadeFactor = clamp(
        (distanceToCamera - ubo.fadeParams.x) / fadeRange,
        0.0,
        1.0);

    float ditherNoise = fract(
        52.9829189 *
        fract(dot(
            gl_FragCoord.xy,
            vec2(0.06711056, 0.00583715))));

    if (ditherNoise > 1.0 - fadeFactor) {
        discard;
    }

    // Instanced trees, rocks, and foliage use a single directional light
    // plus ambient. They do not run the point-light loop, normal-map lookup,
    // ORM lookup, GGX distribution, or specular BRDF.
    vec3 normal = normalize(fragNormal);
    vec3 sunDirection = normalize(ubo.sunDirection.xyz);

    float diffuse = max(dot(normal, sunDirection), 0.0);

    // A small wrap term keeps the underside of foliage readable.
    float wrappedDiffuse = diffuse * 0.85 + 0.15;

    vec3 lighting =
        vec3(ubo.ambient * 0.9) +
        ubo.sunColor.rgb *
        ubo.sunColor.a *
        wrappedDiffuse;

    outColor = vec4(
        ApplyFog(albedo.rgb * lighting, distanceToCamera),
        albedo.a);
}