#version 450
#extension GL_EXT_nonuniform_qualifier : require

#include "common_structures.glsl"

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec3 fragNormal;
layout(location = 4) in vec3 fragTangent;

layout(push_constant) uniform PushConstants {
    vec3 cameraPos;
    float time;
} pc;

layout(set = 0, binding = 3) uniform sampler2D normalSamplers[]; 

layout(location = 0) out vec4 outColor;

void main() {
    vec3 flowDir = normalize(fragTangent); 
    
    if (length(flowDir) < 0.01) {
        flowDir = normalize(vec3(1.0, 0.0, 1.0)); 
    }
    
    vec2 offset1 = vec2(pc.time * 0.05, pc.time * 0.05);
    vec2 offset2 = vec2(pc.time * -0.02, pc.time * 0.04) + vec2(0.4, 0.6);

    vec3 n1 = texture(normalSamplers[nonuniformEXT(0)], fragTexCoord + offset1).xyz * 2.0 - 1.0;
    vec3 n2 = texture(normalSamplers[nonuniformEXT(0)], fragTexCoord * 1.5 + offset2).xyz * 2.0 - 1.0;
    
    vec3 blendedNormal = normalize(n1 + n2);
    vec3 worldNormal = normalize(vec3(blendedNormal.x, 1.0, blendedNormal.y)); 

    vec3 viewDir = normalize(ubo.cameraPos - fragPos);
    vec3 lightDir = normalize(ubo.sunDirection.xyz);
    vec3 halfDir = normalize(lightDir + viewDir);

    float specAmount = pow(max(dot(worldNormal, halfDir), 0.0), 256.0);
    vec3 specular = ubo.sunColor.rgb * specAmount * 2.5; 

    float diffuse = max(dot(worldNormal, lightDir), 0.0);
    vec3 ambient = fragColor * ubo.ambient;
    
    vec3 finalColor = ambient + (fragColor * diffuse * 0.4) + specular;

    float dist = length(ubo.cameraPos - fragPos);
    float fogFactor = clamp((dist - ubo.fogStart) / (ubo.fogEnd - ubo.fogStart), 0.0, 1.0);
    
    vec3 fogColor = vec3(0.55, 0.65, 0.75); 
    finalColor = mix(finalColor, fogColor, fogFactor);

    outColor = vec4(finalColor, 0.85);
}