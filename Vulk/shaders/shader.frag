#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(early_fragment_tests) in;

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    vec3 lightDir;
    vec3 lightColor;
    float ambient;
    float specularPower;
} ubo;

layout(binding = 1) uniform sampler2D globalTextures[];

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint fragTextureId;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(globalTextures[fragTextureId], fragTexCoord);
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(ubo.lightDir);
    vec3 V = normalize(ubo.cameraPos - fragWorldPos);
    vec3 H = normalize(L + V);

    // Ambient
    vec3 ambientColor = ubo.ambient * texColor.rgb;

    // Diffuse
    float diff = max(dot(N, L), 0.0);
    vec3 diffuseColor = diff * ubo.lightColor * texColor.rgb;

    // Specular (Blinn-Phong)
    float spec = pow(max(dot(N, H), 0.0), ubo.specularPower);
    vec3 specularColor = spec * ubo.lightColor * 0.3;

    // Combine
    vec3 finalColor = ambientColor + diffuseColor + specularColor;

    outColor = vec4(finalColor, texColor.a);
}