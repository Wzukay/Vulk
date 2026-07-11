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
    // ... rest of UBO
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
    // ---- Texture fetch ----
    vec4 texColor = texture(textureSamplers[push.textureId], inUV);
    if (texColor.a < 0.3) discard;

    // ---- Base fade (fix: fade the base, not the tip) ----
    float baseFade = smoothstep(0.0, 0.15, 1.0 - inUV.y); // 1 at tip, 0 at base
    float alpha = texColor.a * baseFade;

    // ---- LOD fade: smoothly reduce alpha as lodFactor goes to 1 ----
    float alphaFade = 1.0 - push.lodFactor;
    alpha *= alphaFade;
    if (alpha < 0.02) discard;

    // ---- Per‑blade random ----
    float bladeRand = inBladeRand;
    float maturity = smoothstep(0.3, 0.7, bladeRand);

    // ---- Height gradient ----
    float height = 1.0 - inUV.y;

    // ---- Colours ----
    vec3 youngTip  = vec3(0.545, 0.765, 0.290);
    vec3 youngBase = vec3(0.106, 0.369, 0.125);
    vec3 oldTip    = vec3(0.741, 0.718, 0.420);
    vec3 oldBase   = vec3(0.420, 0.557, 0.137);

    vec3 tipColor   = mix(youngTip, oldTip, maturity);
    vec3 baseColor  = mix(youngBase, oldBase, maturity);

    float gradientStrength = mix(1.0, 0.3, maturity);
    vec3 heightGradient = mix(baseColor, tipColor, height * gradientStrength + (1.0 - gradientStrength) * 0.5);

    vec3 finalColor = texColor.rgb * heightGradient;

    // ---- Normal ----
    vec3 N = normalize(inNormal);
    float noise1 = fract(sin(inWorldPos.x * 127.1 + inWorldPos.z * 311.7) * 43758.5453);
    float noise2 = fract(sin(inWorldPos.x * 269.5 + inWorldPos.z * 183.3) * 43758.5453);
    vec3 perturb = vec3(noise1 - 0.5, noise2 - 0.5, 0.0) * 0.08;
    N = normalize(N + perturb);

    // ---- Lighting vectors ----
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 viewDir  = normalize(ubo.cameraPos - inWorldPos);
    vec3 halfVec  = normalize(lightDir + viewDir);

    // ---- Diffuse & ambient ----
    float diff = max(0.0, dot(N, lightDir));
    float ambient = ubo.ambient * (0.5 + 0.5 * inUV.y);

    // ---- Base lit colour ----
    vec3 litColor = finalColor * (ambient + diff);

    // ---- LOD simplification: skip expensive effects for distant grass ----
    if (push.lodFactor < 0.5) {
        // Close grass: full effects
        // Translucency
        float backLight = max(0.0, dot(viewDir, -lightDir));
        vec3 transmitColor = vec3(0.6, 0.9, 0.2);
        float transmitStrength = 0.35 * (1.0 - maturity * 0.6);
        float tipTransmit = 0.6 + 0.4 * height;
        vec3 translucency = finalColor * transmitColor * backLight * transmitStrength * tipTransmit;
        litColor += translucency;

        // Specular
        float specExponent = mix(32.0, 8.0, maturity);
        float specIntensity = 0.12 * mix(1.0, 0.3, maturity);
        float spec = pow(max(0.0, dot(N, halfVec)), specExponent);
        vec3 specColor = spec * specIntensity * vec3(1.0, 0.95, 0.85);
        litColor += specColor;

        // Fresnel rim light
        float fresnel = pow(1.0 - max(0.0, dot(N, viewDir)), 3.0);
        vec3 rimColor = vec3(0.3, 0.5, 0.1) * 0.3;
        litColor += rimColor * fresnel * (0.5 + 0.5 * (1.0 - maturity));

        // Color temperature
        vec3 warmColor = vec3(1.0, 0.95, 0.85);
        vec3 coolColor = vec3(0.7, 0.8, 1.0);
        float sunFactor = max(0.0, dot(N, lightDir));
        vec3 temperature = mix(coolColor, warmColor, sunFactor);
        litColor *= temperature;
    } else {
        // Distant grass: simple lit colour only (no translucency, specular, fresnel, temperature)
        // We already have litColor = ambient + diffuse, that's enough.
        // Optionally, add a slight fog effect, but we already fade alpha.
        // We can also darken or desaturate slightly to blend.
        litColor *= 0.9; // slightly darker to blend with distance
    }

    // ---- Final output ----
    outColor = vec4(litColor, alpha);
}