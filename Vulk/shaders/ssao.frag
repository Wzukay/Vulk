#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D depthMap;
layout(binding = 1) uniform SSAOUBO {
    mat4 projection;
    mat4 inverseProjection;
    vec4 samples[24];
    vec4 noise[16]; // Kept to maintain C++ struct alignment
} ubo;
layout(binding = 2) uniform sampler2D texNoise;

layout(push_constant) uniform PushConstants {
    vec2 screenSize;
    float radius;
    float bias;
    vec2 renderScale;
} pc;

vec3 GetViewPos(vec2 screenUV) {
    vec2 activeUV = screenUV * pc.renderScale;
    float rawDepth = texture(depthMap, activeUV).r;
    
    vec4 clipSpace = vec4(screenUV * 2.0 - 1.0, rawDepth, 1.0);
    vec4 viewSpace = ubo.inverseProjection * clipSpace;
    return viewSpace.xyz / viewSpace.w;
}

vec3 GetNormalFromDepth(vec2 screenUV, vec3 p0) {
    vec2 screenTexelSize = 1.0 / pc.screenSize;
    vec3 p1 = GetViewPos(screenUV + vec2(screenTexelSize.x, 0.0));
    vec3 p2 = GetViewPos(screenUV + vec2(0.0, screenTexelSize.y));
    
    vec3 normal = cross(p2 - p0, p1 - p0);
    
    if (length(normal) < 0.0001) {
        return vec3(0.0, 0.0, 1.0);
    }
    
    return normalize(normal);
}

void main() {
    vec2 activeUV = fragUV * pc.renderScale;
    float rawDepth = texture(depthMap, activeUV).r;
    
    if (rawDepth >= 1.0) {
        outColor = vec4(1.0);
        return;
    }

    vec3 fragPos = GetViewPos(fragUV);
    vec3 normal = GetNormalFromDepth(fragUV, fragPos);
    
    // FIXED: Use exact pixel coordinates to prevent UV precision banding
    ivec2 noiseDim = textureSize(texNoise, 0);
    ivec2 noiseCoord = ivec2(gl_FragCoord.xy) % noiseDim;
    vec3 randomVec = texelFetch(texNoise, noiseCoord, 0).xyz;

    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    
    for (int i = 0; i < 24; ++i) {
        vec3 samplePos = fragPos + (TBN * ubo.samples[i].xyz) * pc.radius;
        
        vec4 offset = vec4(samplePos, 1.0);
        offset = ubo.projection * offset;
        offset.xyz /= offset.w;
        offset.xy = offset.xy * 0.5 + 0.5;
        
        if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0) {
            continue;
        }
        
        vec2 sampleActiveUV = offset.xy * pc.renderScale;
        float rawSampleDepth = texture(depthMap, sampleActiveUV).r;
        
        vec4 sampleClip = vec4(offset.xy * 2.0 - 1.0, rawSampleDepth, 1.0);
        vec4 sampleView = ubo.inverseProjection * sampleClip;
        float sampleViewZ = sampleView.z / sampleView.w;
        
        // FIXED: Correct distance falloff formula
        float distance = abs(fragPos.z - sampleViewZ);
        float rangeCheck = smoothstep(0.0, 1.0, 1.0 - (distance / pc.radius));
        
        float occlusionCheck = step(samplePos.z + pc.bias, sampleViewZ);
        
        occlusion += occlusionCheck * rangeCheck;
    }
    
    occlusion = 1.0 - (occlusion / 24.0);
    outColor = vec4(occlusion, occlusion, occlusion, 1.0);
}