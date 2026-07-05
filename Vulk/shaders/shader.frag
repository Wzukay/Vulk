#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(early_fragment_tests) in;

// Unsized bindless descriptor sampler array
layout(binding = 1) uniform sampler2D globalTextures[];

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint fragTextureId;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(vec3(0.4, 1.0, 0.6));
    float diff = max(dot(N, L), 0.0);

    // Look up texture using the successfully passed index
    vec4 texColor = texture(globalTextures[fragTextureId], fragTexCoord);
    
    outColor = vec4(texColor.rgb * (0.3 + 0.7 * diff), texColor.a);
}