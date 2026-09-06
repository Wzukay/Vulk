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
    vec2 textureSizePixels = vec2(textureSize(sceneTexture, 0));
    vec2 inverseTextureSize = 1.0 / textureSizePixels;
    vec2 activeUV = fragUV * pc.renderScale;

    vec3 finalColor;
    float sceneAlpha = texture(sceneTexture, activeUV).a;

    if (pc.sharpness <= 0.0) {
        finalColor = texture(sceneTexture, activeUV).rgb;
    } else {
        vec3 center = texture(sceneTexture, activeUV).rgb;
        vec3 bottom = texture(sceneTexture, activeUV + vec2(0.0, -inverseTextureSize.y)).rgb;
        vec3 left = texture(sceneTexture, activeUV + vec2(-inverseTextureSize.x, 0.0)).rgb;
        vec3 right = texture(sceneTexture, activeUV + vec2(inverseTextureSize.x, 0.0)).rgb;
        vec3 top = texture(sceneTexture, activeUV + vec2(0.0, inverseTextureSize.y)).rgb;

        vec3 minimumColor = min(min(min(bottom, left), right), top);
        vec3 maximumColor = max(max(max(bottom, left), right), top);

        vec3 minSafe = min(minimumColor, 1.0 - maximumColor);
        vec3 maxSafe = max(maximumColor, 0.00001);
        vec3 amp = clamp(minSafe / maxSafe, 0.0, 1.0);

        float safeSharpness = clamp(pc.sharpness, 0.0, 1.0) * 0.16;
        vec3 weight = sqrt(amp) * -safeSharpness;

        finalColor = clamp((center + (bottom + left + right + top) * weight) / (1.0 + 4.0 * weight), 0.0, 1.0);
    }

    if (pc.enableSSAO == 1) {
        finalColor *= texture(ssaoMap, activeUV).r;
    }

    vec4 waterColor = texture(waterTexture, fragUV);
    finalColor = mix(finalColor, waterColor.rgb, waterColor.a);

    outColor = vec4(finalColor, sceneAlpha);
}