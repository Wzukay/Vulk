#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D depthMap;
layout(binding = 1) uniform SSAOUBO {
    mat4 projection;
    mat4 inverseProjection;
    vec4 samples[24];
    vec4 noise[16];
} ubo;

layout(push_constant) uniform PushConstants {
    vec2 screenSize;
    float radius;
    float bias;
} pc;

// Robust matrix unprojection
vec3 GetViewPos(vec2 uv) {
    float rawDepth = texture(depthMap, uv).r;
    vec4 clipSpace = vec4(uv * 2.0 - 1.0, rawDepth, 1.0);
    vec4 viewSpace = ubo.inverseProjection * clipSpace;
    return viewSpace.xyz / viewSpace.w;
}

vec3 GetNormalFromDepth(vec2 uv, vec3 p0) {
    vec2 texelSize = 1.0 / pc.screenSize;
    
    vec3 p1 = GetViewPos(uv + vec2(texelSize.x, 0.0));
    vec3 p2 = GetViewPos(uv + vec2(0.0, texelSize.y));
    
    // FIXED: Swapped p2 and p1 to account for Vulkan's Y-down coordinate 
    // system. This forces the normal to point out towards the camera (+Z).
    vec3 normal = cross(p2 - p0, p1 - p0);
    
    // Fallback to prevent NaN black pixels if sampling off the edge of the screen
    if (length(normal) < 0.0001) {
        return vec3(0.0, 0.0, 1.0);
    }
    
    return normalize(normal);
}

void main() {
    float rawDepth = texture(depthMap, fragUV).r;
    
    // Discard skybox/background fragments at the far plane
    if (rawDepth >= 1.0) {
        outColor = vec4(1.0);
        return;
    }

    vec3 fragPos = GetViewPos(fragUV);
    vec3 normal = GetNormalFromDepth(fragUV, fragPos);
    
    ivec2 noiseCoord = ivec2(mod(gl_FragCoord.xy, 4.0));
    vec3 randomVec = normalize(ubo.noise[noiseCoord.y * 4 + noiseCoord.x].xyz);

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
        
        // Matrix unprojection for the sample point depth
        float rawSampleDepth = texture(depthMap, offset.xy).r;
        vec4 sampleClip = vec4(offset.xy * 2.0 - 1.0, rawSampleDepth, 1.0);
        vec4 sampleView = ubo.inverseProjection * sampleClip;
        float sampleViewZ = sampleView.z / sampleView.w;
        
        float rangeCheck = smoothstep(0.0, 1.0, pc.radius / abs(fragPos.z - sampleViewZ));
        float occlusionCheck = step(samplePos.z + pc.bias, sampleViewZ);
        
        occlusion += occlusionCheck * rangeCheck;
    }
    
    occlusion = 1.0 - (occlusion / 24.0);
    
    outColor = vec4(occlusion, occlusion, occlusion, 1.0);
}