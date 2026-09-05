#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneTexture;
layout(binding = 1) uniform sampler2D ssaoMap;
layout(binding = 2) uniform sampler2D waterTexture;

layout(push_constant) uniform FSRPushConstants {
    vec4 Const0;
    vec4 Const1;
    vec4 Const2;
    vec4 Const3;
    float sharpness;
    uint enableSSAO;
    vec2 renderScale;
} pc;

void main() {
    vec2 activeUV = fragUV * pc.renderScale;

    vec4 sceneColor = texture(sceneTexture, activeUV);
    vec4 waterColor = texture(waterTexture, fragUV);

    if (pc.enableSSAO == 1) {
        sceneColor.rgb *= texture(ssaoMap, activeUV).r;
    }

    sceneColor.rgb = mix(sceneColor.rgb, waterColor.rgb, waterColor.a);
    outColor = sceneColor;
}