#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    float ambient;
    float specularPower;
    uint lightCount;
    float fogStart;
    float fogEnd;
    vec2 screenSize;   // <-- must match C++ struct
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = gl_FragCoord.xy / ubo.screenSize;
    vec4 clipPos = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 worldPos = inverse(ubo.proj * ubo.view) * clipPos;
    vec3 viewDir = normalize(worldPos.xyz / worldPos.w - ubo.cameraPos);
    float y = viewDir.y * 0.5 + 0.5;
    vec3 horizonColor = vec3(0.6, 0.7, 0.8);
    vec3 zenithColor = vec3(0.1, 0.2, 0.4);
    vec3 color = mix(horizonColor, zenithColor, y);
    outColor = vec4(color, 1.0);
}