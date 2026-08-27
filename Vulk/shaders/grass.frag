#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in float inBladeRand;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    float ambient;
    vec4 fadeParams;
    vec2 screenSize;
    float specularPower;
    uint lightCount;
    float fogStart;
    float fogEnd;
    mat4 inverseViewProj;
    mat4 inverseProj;
    mat4 inverseView;
} ubo;

layout(set = 0, binding = 2) uniform sampler2D textureSamplers[128];

layout(push_constant) uniform PushConstants {
    float time;
    uint textureId;
    float windStrength;
    float windSpeed;
    float lodFactor;
} push;

void main() {
    vec4 texColor = texture(textureSamplers[push.textureId], inUV);

    // 1. Base Cutout: Discard transparent pixels or black backgrounds (JPG hack)
    if (texColor.a < 0.3 || length(texColor.rgb) < 0.15) discard;

    // 2. Screen-Space Dithered LOD Dissolve (4x4 Bayer Matrix)
    float ditherNoise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float distToCam = length(ubo.cameraPos - inWorldPos);

    float maxFadeDistance = ubo.fadeParams.w; 
    float fadeStartDistance = ubo.fadeParams.z; 
    
    float fadeAlpha = 1.0 - clamp((distToCam - fadeStartDistance) / (maxFadeDistance - fadeStartDistance), 0.0, 1.0);
    
    if (ditherNoise > fadeAlpha) {
        discard;
    }

    // 3. Simple, clean lighting
    vec3 N = normalize(inNormal);
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diff = max(0.0, dot(N, lightDir));
    
    float ambient = ubo.ambient + 0.3; 
    vec3 litColor = texColor.rgb * (ambient + diff * 0.7);

    float fogFactor = clamp((distToCam - ubo.fogStart) / (ubo.fogEnd - ubo.fogStart), 0.0, 1.0);
    if (distToCam > ubo.fogEnd) fogFactor = 1.0;
    
    vec3 fogColor = vec3(0.6, 0.7, 0.8);
    vec3 finalColor = mix(litColor, fogColor, fogFactor);

    outColor = vec4(finalColor, 1.0);
}