#version 450
#extension GL_EXT_nonuniform_qualifier : require

struct Light {
    vec4 positionOrDir;
    vec4 color;
    vec4 params;
};

layout(early_fragment_tests) in;

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    float ambient;
    float specularPower;
    uint lightCount;
} ubo;

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

vec3 CalcBlinnPhong(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, float specPower) {
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), specPower);
    return diff * lightColor * albedo + spec * lightColor * 0.3;
}

float CalcAttenuation(float dist, float range) {
    float atten = 1.0 - smoothstep(0.0, range, dist);
    return atten * atten / (dist * dist + 1.0);
}

void main() {
    vec4 texColor = texture(globalTextures[fragTextureId], fragTexCoord);
    vec3 albedo = texColor.rgb;

    vec3 N_geo = normalize(fragNormal);
    vec3 T = normalize(fragTangent);
    T = normalize(T - N_geo * dot(N_geo, T)); // re-orthogonalize after interpolation
    vec3 B = cross(N_geo, T) * fragTangentHandedness;
    mat3 TBN = mat3(T, B, N_geo);

    // Sample normal map (using same texture id as diffuse for now — see note below)
    vec3 tangentNormal = texture(normalTextures[fragNormalTextureId], fragTexCoord).rgb * 2.0 - 1.0;
    vec3 N = normalize(TBN * tangentNormal);

    vec3 V = normalize(ubo.cameraPos - fragWorldPos);
    vec3 result = ubo.ambient * albedo;

    for (uint i = 0u; i < ubo.lightCount; ++i) {
        Light L = lightBuffer.lights[i];
        vec3 lightVec;
        float atten = 1.0;

        if (L.positionOrDir.w > 0.5) {
            vec3 toLight = L.positionOrDir.xyz - fragWorldPos;
            float dist = length(toLight);
            lightVec = toLight / max(dist, 0.0001);
            atten = CalcAttenuation(dist, max(L.params.x, 0.001));
        } else {
            lightVec = normalize(L.positionOrDir.xyz);
        }

        vec3 radiance = L.color.rgb * L.color.a * atten;
        result += CalcBlinnPhong(N, V, lightVec, radiance, albedo, ubo.specularPower);
    }

    outColor = vec4(result, texColor.a);
}