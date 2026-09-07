#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneTexture;
layout(binding = 1) uniform sampler2D ssaoMap;
layout(binding = 2) uniform sampler2D waterTexture;

layout(push_constant) uniform FSRConstants {
    float sharpness;
    int enableSSAO;
    vec2 renderScale;
} pc;

float rgb2luma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 activeUV = fragUV * pc.renderScale;
    
    // FIXED: Changed sceneColor to sceneTexture
    vec2 invScreenSize = 1.0 / vec2(textureSize(sceneTexture, 0));

    vec3 colorCenter = texture(sceneTexture, activeUV).rgb;
    float alphaCenter = texture(sceneTexture, activeUV).a;

    // FXAA parameters
    float FXAA_SPAN_MAX = 8.0;
    float FXAA_REDUCE_MUL = 1.0 / 8.0;
    float FXAA_REDUCE_MIN = 1.0 / 128.0;

    // Sample surrounding pixels
    vec3 lumaN = texture(sceneTexture, activeUV + vec2(0.0, -invScreenSize.y)).rgb;
    vec3 lumaS = texture(sceneTexture, activeUV + vec2(0.0, invScreenSize.y)).rgb;
    vec3 lumaW = texture(sceneTexture, activeUV + vec2(-invScreenSize.x, 0.0)).rgb;
    vec3 lumaE = texture(sceneTexture, activeUV + vec2(invScreenSize.x, 0.0)).rgb;

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

    dir = min(vec2(FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX), dir * rcpDirMin)) * invScreenSize;

    vec3 rgbA = 0.5 * (
        texture(sceneTexture, activeUV + dir * (1.0/3.0 - 0.5)).rgb +
        texture(sceneTexture, activeUV + dir * (2.0/3.0 - 0.5)).rgb);

    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(sceneTexture, activeUV + dir * -0.5).rgb +
        texture(sceneTexture, activeUV + dir * 0.5).rgb);

    float lumaB = rgb2luma(rgbB);
    
    vec3 finalColor;
    if (lumaB < lumaMin || lumaB > lumaMax) {
        finalColor = rgbA; // Standard FXAA fallback uses rgbA instead of colorCenter
    } else {
        finalColor = rgbB;
    }

    // Apply SSAO
    if (pc.enableSSAO == 1) {
        finalColor *= texture(ssaoMap, activeUV).r;
    }

    // Apply Water
    vec4 waterColor = texture(waterTexture, fragUV);
    finalColor = mix(finalColor, waterColor.rgb, waterColor.a);

    outColor = vec4(finalColor, alphaCenter);
}