#version 450
#extension GL_EXT_nonuniform_qualifier : require

struct Light {
    vec4 positionOrDir;
    vec4 color;
    vec4 params;
};

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    float ambient;
    float specularPower;
    uint lightCount;
} ubo;

layout(std430, binding = 1) readonly buffer LightBuffer {
    Light lights[];
} lightBuffer;

layout(binding = 2) uniform sampler2D globalTextures[];
layout(binding = 3) uniform sampler2D normalTextures[];

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in float fragTangentHandedness;
layout(location = 7) in vec3 fragColor; // Splat weights (r=sand, g=grass, b=rock)

layout(location = 0) out vec4 outColor;

// 🚀 TRIPLANAR SAMPLING HELPER
// Samples a texture three times based on world position and blends by normal
vec4 TriplanarSample(uint textureId, vec3 pos, vec3 weights) {
    vec4 x = texture(globalTextures[textureId], pos.yz);
    vec4 y = texture(globalTextures[textureId], pos.xz);
    vec4 z = texture(globalTextures[textureId], pos.xy);
    return x * weights.x + y * weights.y + z * weights.z;
}

vec3 CalcBlinnPhong(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, float specPower) {
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), specPower);
    return diff * lightColor * albedo + spec * lightColor * 0.1;
}

void main() {
    // 1. Calculate Triplanar Weights (steepness/normal direction)
    vec3 blendWeights = abs(fragNormal);
    blendWeights /= (blendWeights.x + blendWeights.y + blendWeights.z);

    // 2. Sample Textures (Indices 1, 2, 3 as per your logs)
    // We use Triplanar for Rock (3) to prevent cliff stretching
    vec4 sand  = texture(globalTextures[1], fragTexCoord);
    vec4 grass = texture(globalTextures[2], fragTexCoord);
    vec4 rock  = TriplanarSample(3, fragWorldPos * 0.05, blendWeights);

    vec4 albedo = vec4(0.0);

    if (fragColor.r > 0.01) {
        albedo += texture(globalTextures[1], fragTexCoord) * fragColor.r;
    }
    if (fragColor.g > 0.01) {
        albedo += texture(globalTextures[2], fragTexCoord) * fragColor.g;
    }
    if (fragColor.b > 0.01) {
        // Only use Triplanar for Rock if it's actually visible
        albedo += TriplanarSample(3, fragWorldPos * 0.05, blendWeights) * fragColor.b;
    }

    // 3. Normal Mapping
    vec3 N_geo = normalize(fragNormal);
    vec3 T = normalize(fragTangent);
    T = normalize(T - N_geo * dot(N_geo, T)); 
    vec3 B = cross(N_geo, T) * fragTangentHandedness;
    mat3 TBN = mat3(T, B, N_geo);

    vec3 nSand  = texture(normalTextures[4], fragTexCoord).rgb * 2.0 - 1.0;
    vec3 nGrass = texture(normalTextures[5], fragTexCoord).rgb * 2.0 - 1.0;
    vec3 nRock  = texture(normalTextures[6], fragTexCoord).rgb * 2.0 - 1.0; // Can also be Triplanar-sampled
    
    vec3 blendedNormal = (nSand * fragColor.r) + (nGrass * fragColor.g) + (nRock * fragColor.b);
    vec3 N = normalize(TBN * blendedNormal);

    // 4. Lighting Loop
    vec3 V = normalize(ubo.cameraPos - fragWorldPos);
    vec3 result = ubo.ambient * albedo.xyz;

    for (uint i = 0u; i < ubo.lightCount; ++i) {
        Light L = lightBuffer.lights[i];
        vec3 lightVec = normalize(L.positionOrDir.xyz - (L.positionOrDir.w > 0.5 ? fragWorldPos : vec3(0.0)));
        float atten = (L.positionOrDir.w > 0.5) ? (1.0 / length(L.positionOrDir.xyz - fragWorldPos)) : 1.0;
        
        result += CalcBlinnPhong(N, V, lightVec, L.color.rgb * L.color.a * atten, albedo.xyz, ubo.specularPower);
    }

    outColor = vec4(result, 1.0);
}