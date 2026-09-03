#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D ssaoInput;
layout(binding = 1) uniform sampler2D depthMap;

layout(push_constant) uniform PushConstants {
    vec2 screenSize;
    vec2 blurDirection; // (1.0, 0.0) for horizontal, (0.0, 1.0) for vertical
    float colorSigma;
    float spatialSigma;
} pc;

void main() {
    vec2 texelSize = (1.0 / pc.screenSize) * pc.blurDirection;
    float centerAO = texture(ssaoInput, fragUV).r;
    float centerDepth = texture(depthMap, fragUV).r;
    
    float result = 0.0;
    float totalWeight = 0.0;
    
    // 5-tap directional blur (-2 to 2)
    for (int r = -2; r <= 2; ++r) {
        vec2 sampleUV = fragUV + float(r) * texelSize;
        
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            continue;
        }
        
        float sampleAO = texture(ssaoInput, sampleUV).r;
        float sampleDepth = texture(depthMap, sampleUV).r;
        
        float colorDiff = abs(centerAO - sampleAO);
        float colorWeight = exp(-colorDiff * colorDiff / (2.0 * pc.colorSigma * pc.colorSigma));
        
        float spatialDist = float(abs(r));
        float spatialWeight = exp(-spatialDist * spatialDist / (2.0 * pc.spatialSigma * pc.spatialSigma));
        
        float depthDiff = abs(centerDepth - sampleDepth);
        float depthWeight = exp(-depthDiff * 100.0);
        
        float weight = colorWeight * spatialWeight * depthWeight;
        result += sampleAO * weight;
        totalWeight += weight;
    }
    
    result /= max(totalWeight, 0.0001);
    outColor = vec4(result, result, result, 1.0);
}