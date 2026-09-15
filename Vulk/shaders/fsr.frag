#version 450
#include "common_structures.glsl"

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D sceneTexture;
layout(binding = 2) uniform sampler2D ssaoMap;
layout(binding = 3) uniform sampler2D waterTexture;
layout(binding = 4) uniform sampler2D depthSampler; 

layout(push_constant) uniform FSRPushConstants {
    vec4 Const0;
    vec4 Const1;
    vec4 Const2;
    vec4 Const3;
    float sharpness;
    uint enableSSAO;
    vec2 renderScale;
    uint enableMotionBlur;
    uint enableGodRays;
    vec2 lightScreenPos;
    vec4 lightColorAndIntensity;
} pc;

float InterleavedGradientNoise(vec2 pixelPos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(pixelPos, magic.xy)));
}

vec3 GetGodRays(vec2 uv) {
    float intensity = pc.lightColorAndIntensity.a;
    if (pc.enableGodRays == 0 || intensity <= 0.0) return vec3(0.0);

    vec2 lightPos = pc.lightScreenPos * pc.renderScale; 
    vec2 deltaUV = (uv - lightPos);
    
    // 1. EARLY OUT: If the pixel is too far from the sun, the rays fade to 0 anyway.
    // Skip the loop entirely for massive chunks of the screen!
    float distFromLight = length(deltaUV);
    if (distFromLight > 0.65) return vec3(0.0);
    
    float density = 0.95;
    // 2. Adjust math to compensate for fewer samples
    float weight = 0.12;     
    float decay = 0.85;      
    float exposure = 0.8;    
    
    // 3. Drop from 16 to 7 samples. The IGN dither + FSR will hide this beautifully!
    const int NUM_SAMPLES = 7; 

    deltaUV *= 1.0 / float(NUM_SAMPLES) * density;
    
    float jitter = InterleavedGradientNoise(uv * ubo.screenSize);
    vec2 currentUV = uv - (deltaUV * jitter);
    
    vec3 accum = vec3(0.0);
    float illuminationDecay = 1.0;

    for(int i = 0; i < NUM_SAMPLES; i++) {
        currentUV -= deltaUV;
        vec2 sampleUV = clamp(currentUV, vec2(0.001), pc.renderScale - 0.001);
        
        vec4 sampleData = texture(sceneTexture, sampleUV);
        float isSky = 1.0 - sampleData.a; 
        
        float brightness = max(max(sampleData.r, sampleData.g), sampleData.b);
        vec3 maskedColor = sampleData.rgb * smoothstep(0.2, 0.45, brightness);

        accum += maskedColor * isSky * illuminationDecay * weight;
        illuminationDecay *= decay;
    }

    vec3 tint = pc.lightColorAndIntensity.rgb;
    
    // 4. Smoothly fade the edges of the early-out radius so it doesn't leave a hard circle line
    float radialFade = 1.0 - smoothstep(0.45, 0.65, distFromLight);
    
    return accum * exposure * intensity * tint * radialFade;
}

vec4 GetMotionBlurredColor(vec2 screenUV, vec2 textureUV) {
    if (pc.enableMotionBlur == 0) {
        return texture(sceneTexture, textureUV);
    }

    float depth = texture(depthSampler, textureUV).r;
    vec4 color = texture(sceneTexture, textureUV);
    
    if (depth >= 1.0) return color; 

    vec4 clipSpacePos = vec4(screenUV * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = ubo.inverseViewProj * clipSpacePos;
    worldPos /= worldPos.w;

    vec4 prevClipSpace = ubo.previousViewProj * worldPos;
    vec2 prevNDC = prevClipSpace.xy / prevClipSpace.w;
    vec2 prevScreenUV = prevNDC * 0.5 + 0.5;

    vec2 prevTextureUV = prevScreenUV * pc.renderScale;
    vec2 velocity = (textureUV - prevTextureUV) * 0.5; 
    
    float maxVelocity = 0.03;
    if (length(velocity) > maxVelocity) {
        velocity = normalize(velocity) * maxVelocity;
    }

    // 1. Calculate a unique jitter offset for this specific pixel
    float jitter = InterleavedGradientNoise(screenUV * ubo.screenSize);

    // 2. Drop the sample count for massive performance gains!
    const int BLUR_SAMPLES = 5; 
    
    for (int i = 1; i < BLUR_SAMPLES; i++) {
        // 3. Subtract the jitter from the loop index to dither the sample spacing
        float offsetPercent = (float(i) - jitter) / float(BLUR_SAMPLES - 1);
        vec2 offsetUV = textureUV - (velocity * offsetPercent);
        
        offsetUV = clamp(offsetUV, vec2(0.001), pc.renderScale - 0.001); 
        color += texture(sceneTexture, offsetUV);
    }
    
    return color / float(BLUR_SAMPLES);
}

void main() {
    vec2 textureSizePixels = vec2(textureSize(sceneTexture, 0));
    vec2 inverseTextureSize = 1.0 / textureSizePixels;
    vec2 activeUV = fragUV * pc.renderScale;

    vec3 finalColor;
    float sceneAlpha = texture(sceneTexture, activeUV).a;

   if (pc.sharpness <= 0.0) {
        finalColor = GetMotionBlurredColor(fragUV, activeUV).rgb;
    } else {
        vec3 center = GetMotionBlurredColor(fragUV, activeUV).rgb;
        
        // Use cheap standard samples for the FSR edge detection boundaries
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

    finalColor.rgb += GetGodRays(activeUV);

    vec4 waterColor = texture(waterTexture, activeUV);
    finalColor = mix(finalColor, waterColor.rgb, waterColor.a);

    outColor = vec4(finalColor, sceneAlpha);
}