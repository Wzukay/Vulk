#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    uint textureId;
} pc;

layout(set = 0, binding = 2) uniform sampler2D textures[]; 

void main() {
    vec4 texColor = texture(textures[pc.textureId], fragUV);
    
    if (texColor.a < 0.5) {
        discard;
    }
    
    // Apply the random tint to the butterfly's texture
    outColor = vec4(texColor.rgb * fragColor, texColor.a);
}