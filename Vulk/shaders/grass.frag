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
    layout(offset = 128) vec3 cameraPos;
    layout(offset = 140) float ambient;
    layout(offset = 144) float specularPower;
    layout(offset = 148) uint lightCount;
    layout(offset = 152) float fogStart;
    layout(offset = 156) float fogEnd;
    layout(offset = 160) vec2 screenSize;
    layout(offset = 176) mat4 inverseViewProj;
    layout(offset = 240) mat4 inverseProj;
    layout(offset = 304) mat4 inverseView;
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
    // Maps pixel screen coordinates to a smooth, uniform discard pattern
    const float bayerMatrix[16] = float[16](
         0.0/16.0, 12.0/16.0,  3.0/16.0, 15.0/16.0,
         8.0/16.0,  4.0/16.0, 11.0/16.0,  7.0/16.0,
         2.0/16.0, 14.0/16.0,  1.0/16.0, 13.0/16.0,
        10.0/16.0,  6.0/16.0,  9.0/16.0,  5.0/16.0
    );

    int x = int(mod(gl_FragCoord.x, 4.0));
    int y = int(mod(gl_FragCoord.y, 4.0));
    float ditherThreshold = bayerMatrix[y * 4 + x];

    // As lodFactor increases from 0.0 to 1.0, pixels are smoothly culled out
    if (push.lodFactor > ditherThreshold) discard;

    // 3. Simple, clean lighting
    vec3 N = normalize(inNormal);
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diff = max(0.0, dot(N, lightDir));
    
    float ambient = ubo.ambient + 0.3; 
    vec3 litColor = texColor.rgb * (ambient + diff * 0.7);

    // Output full texture alpha so remaining pixels write cleanly to the depth buffer
    outColor = vec4(litColor, texColor.a);
}