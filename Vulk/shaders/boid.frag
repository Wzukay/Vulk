#version 450
#extension GL_EXT_nonuniform_qualifier : require
#include "common_structures.glsl"

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    uint textureId;
    uint animationType;
    vec2 pad;
    vec4 color1;
    vec4 color2;
} pc;

layout(set = 0, binding = 2) uniform sampler2D textures[]; 

void main() {
    // Sun direction Y: > 0 is day, < 0 is night
    float dayBlend = smoothstep(-0.1, 0.1, ubo.sunDirection.y);
    float nightBlend = smoothstep(0.1, -0.1, ubo.sunDirection.y);

    if (pc.animationType == 0) {
        // --- BUTTERFLIES (Day Only) ---
        if (dayBlend <= 0.01) {
            discard; // Hide at night
        }

        vec4 texColor = texture(textures[nonuniformEXT(pc.textureId)], fragUV);
        if (texColor.a < 0.5) discard;

        vec3 finalColor = texColor.rgb * fragColor;
        outColor = vec4(finalColor, texColor.a * dayBlend);
    } 
    else {
        // --- FIREFLIES (Night Only) ---
        if (nightBlend <= 0.01) {
            discard; // Hide during the day
        }

        vec2 centerUV = fragUV - vec2(0.5);
        float dist = length(centerUV) * 2.0; 
        float alpha = smoothstep(1.0, 0.2, dist); 
        
        if (alpha <= 0.01) discard;

        vec3 glow = fragColor * 8.0; // HDR bloom multiplier
        outColor = vec4(glow * nightBlend, alpha * nightBlend);
    }
}