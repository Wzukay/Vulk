#version 450
#extension GL_EXT_nonuniform_qualifier : require

struct Light {
    vec4 positionOrDir;
    vec4 color;
    vec4 params;
};

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

layout(std430, binding = 1) readonly buffer LightBuffer {
    Light lights[];
} lightBuffer;

// Your massive bindless global texture arrays
layout(binding = 2) uniform sampler2D globalTextures[];
layout(binding = 3) uniform sampler2D normalTextures[];

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint fragTextureId;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in float fragTangentHandedness;
layout(location = 6) flat in uint fragNormalTextureId;

layout(location = 0) out vec4 outColor;

vec3 UnpackNormal(vec4 sampledNormal) {
    vec2 normalXY = sampledNormal.rg * 2.0 - 1.0;
    float normalZ = sqrt(max(0.0, 1.0 - dot(normalXY, normalXY)));
    return vec3(normalXY, normalZ);
}

vec3 CalcBlinnPhong(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, float specPower) {
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), specPower);
    return diff * lightColor * albedo + spec * lightColor * 0.1;
}

void main() {
    // CRITICAL BINDLESS LOOKUP: Samples the specific texture index passed by your asset parser!
    vec4 albedo = texture(globalTextures[nonuniformEXT(fragTextureId)], fragTexCoord);
    
    // Support basic transparency alpha discarding (for leaves, windows, flags in Sponza)
    if (albedo.a < 0.1) {
        discard;
    }

    float ditherNoise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float distToCam = length(ubo.cameraPos - fragWorldPos);
    
    // Trees use X (Start) and Y (End)
    float fadeStartDistance = ubo.fadeParams.x; 
    float maxFadeDistance = ubo.fadeParams.y; 
    
    float fadeAlpha = 1.0 - clamp((distToCam - fadeStartDistance) / (maxFadeDistance - fadeStartDistance), 0.0, 1.0);
    
    if (ditherNoise > fadeAlpha) {
        discard;
    }

    // Tangent Space normal mapping setup
    vec3 N_geo = normalize(fragNormal);
    vec3 T = normalize(fragTangent);
    T = normalize(T - N_geo * dot(N_geo, T)); 
    vec3 B = cross(N_geo, T) * fragTangentHandedness;
    mat3 TBN = mat3(T, B, N_geo);

    // Dynamic Normal map bindless lookup
    vec4 sampledNormal = texture(normalTextures[nonuniformEXT(fragNormalTextureId)], fragTexCoord);
    vec3 normalMap = UnpackNormal(sampledNormal);
    vec3 N = normalize(TBN * normalMap);

    // Blinn-Phong lighting calculation
    vec3 V = normalize(ubo.cameraPos - fragWorldPos);
    vec3 result = ubo.ambient * albedo.xyz;

    for (uint i = 0u; i < ubo.lightCount; ++i) {
        Light L = lightBuffer.lights[i];
        vec3 lightVec = normalize(L.positionOrDir.xyz - (L.positionOrDir.w > 0.5 ? fragWorldPos : vec3(0.0)));
        float atten = (L.positionOrDir.w > 0.5) ? (1.0 / length(L.positionOrDir.xyz - fragWorldPos)) : 1.0;
        
        result += CalcBlinnPhong(N, V, lightVec, L.color.rgb * L.color.a * atten, albedo.xyz, ubo.specularPower);
    }

    // Atmospheric Fog calculation
    float dist = length(ubo.cameraPos - fragWorldPos);
    float fogFactor = clamp((dist - ubo.fadeParams.x) / (ubo.fadeParams.y - ubo.fadeParams.x), 0.0, 1.0);
    if (dist > ubo.fadeParams.y) fogFactor = 1.0;
    vec3 fogColor = vec3(0.6, 0.7, 0.8);
    vec3 finalColor = mix(result, fogColor, fogFactor);

    outColor = vec4(finalColor, 1.0);
}