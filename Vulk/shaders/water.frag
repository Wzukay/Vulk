#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec3 fragNormal;
layout(location = 4) in vec3 fragTangent;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    float ambient;
    float specularPower;
    uint lightCount;
    vec3 cameraPos;
    float fogStart;
    float fogEnd;
    vec4 fadeParams;
    vec2 screenSize;
    mat4 inverseViewProj;
    mat4 inverseProj;
    mat4 inverseView;
    vec4 sunDirection;
    vec4 sunColor;
} ubo;

layout(push_constant) uniform PushConstants {
    vec3 cameraPos;
    float time;
} pc;

// Binding 3 is the normal texture registry in your pipeline
layout(set = 0, binding = 3) uniform sampler2DArray normalSamplers; 

layout(location = 0) out vec4 outColor;

void main() {
    // 1. FLOW CALCULATION
    vec3 flowDir = normalize(fragTangent); 
    
    // Fallback: If water is perfectly flat (lake/ocean), apply a gentle diagonal drift
    if (length(flowDir) < 0.01) {
        flowDir = normalize(vec3(1.0, 0.0, 1.0)); 
    }
    
    // Create two panning offsets based on flow direction and time
    vec2 offset1 = vec2(pc.time * 0.05, pc.time * 0.05);
    vec2 offset2 = vec2(pc.time * -0.02, pc.time * 0.04) + vec2(0.4, 0.6);

    // 2. NORMAL MAPPING
    // IMPORTANT: Swap the '0.0' for whatever texture ID your water normal map uses
    vec3 n1 = texture(normalSamplers, vec3(fragTexCoord + offset1, 0.0)).xyz * 2.0 - 1.0;
    vec3 n2 = texture(normalSamplers, vec3(fragTexCoord * 1.5 + offset2, 0.0)).xyz * 2.0 - 1.0;
    
    // Blend the two normals to break up obvious tiling patterns
    vec3 blendedNormal = normalize(n1 + n2);
    // Convert from tangent space back to world space (assuming flat ground for the base normal)
    vec3 worldNormal = normalize(vec3(blendedNormal.x, 1.0, blendedNormal.y)); 

    // 3. LIGHTING & SPECULAR GLINTS
    vec3 viewDir = normalize(ubo.cameraPos - fragPos);
    vec3 lightDir = normalize(ubo.sunDirection.xyz);
    vec3 halfDir = normalize(lightDir + viewDir);

    // Tight, intense specular highlight for sun reflecting on water
    float specAmount = pow(max(dot(worldNormal, halfDir), 0.0), 256.0);
    vec3 specular = ubo.sunColor.rgb * specAmount * 2.5; 

    float diffuse = max(dot(worldNormal, lightDir), 0.0);
    vec3 ambient = fragColor * ubo.ambient;
    
    vec3 finalColor = ambient + (fragColor * diffuse * 0.4) + specular;

    // 4. FOG INTEGRATION
    float dist = length(ubo.cameraPos - fragPos);
    float fogFactor = clamp((dist - ubo.fogStart) / (ubo.fogEnd - ubo.fogStart), 0.0, 1.0);
    
    // Hardcoded sky color for now, adjust this to match your skybox's horizon tint
    vec3 fogColor = vec3(0.55, 0.65, 0.75); 
    finalColor = mix(finalColor, fogColor, fogFactor);

    // Output with 85% opacity to allow the riverbed to show through
    outColor = vec4(finalColor, 0.85);
}