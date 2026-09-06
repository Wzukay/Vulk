#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneTexture;

layout(push_constant) uniform FXAAPushConstants {
    vec2 invScreenSize;
} pc;

float rgb2luma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec3 colorCenter = texture(sceneTexture, fragUV).rgb;
    float alphaCenter = texture(sceneTexture, fragUV).a;

    // FXAA parameters
    float FXAA_SPAN_MAX = 8.0;
    float FXAA_REDUCE_MUL = 1.0 / 8.0;
    float FXAA_REDUCE_MIN = 1.0 / 128.0;

    // Sample surrounding pixels
    vec3 lumaN = texture(sceneTexture, fragUV + vec2(0.0, -pc.invScreenSize.y)).rgb;
    vec3 lumaS = texture(sceneTexture, fragUV + vec2(0.0, pc.invScreenSize.y)).rgb;
    vec3 lumaW = texture(sceneTexture, fragUV + vec2(-pc.invScreenSize.x, 0.0)).rgb;
    vec3 lumaE = texture(sceneTexture, fragUV + vec2(pc.invScreenSize.x, 0.0)).rgb;

    float lumaCenter = rgb2luma(colorCenter);
    float lumaN_val = rgb2luma(lumaN);
    float lumaS_val = rgb2luma(lumaS);
    float lumaW_val = rgb2luma(lumaW);
    float lumaE_val = rgb2luma(lumaE);

    float lumaMin = min(lumaCenter, min(min(lumaN_val, lumaS_val), min(lumaW_val, lumaE_val)));
    float lumaMax = max(lumaCenter, max(max(lumaN_val, lumaS_val), max(lumaW_val, lumaE_val)));

    vec2 dir;
    dir.x = -((lumaN_val + lumaS_val) - (lumaW_val + lumaE_val));
    dir.y = ((lumaN_val - lumaS_val) + (lumaW_val - lumaE_val));

    float dirReduce = max((lumaN_val + lumaS_val + lumaW_val + lumaE_val) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    dir = min(vec2(FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX), dir * rcpDirMin)) * pc.invScreenSize;

    vec3 rgbA = 0.5 * (
        texture(sceneTexture, fragUV + dir * (1.0/3.0 - 0.5)).rgb +
        texture(sceneTexture, fragUV + dir * (2.0/3.0 - 0.5)).rgb);

    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(sceneTexture, fragUV + dir * -0.5).rgb +
        texture(sceneTexture, fragUV + dir * 0.5).rgb);

    float lumaB = rgb2luma(rgbB);

    if (lumaB < lumaMin || lumaB > lumaMax) {
        outColor = vec4(colorCenter, alphaCenter);
    } else {
        outColor = vec4(rgbB, alphaCenter);
    }
}