#version 450
layout(location = 0) in vec4 inPosScale;
layout(location = 1) in vec4 inVelTime;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragColor; 

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
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

const vec2 quadVertices[6] = vec2[](
    vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 0.5,  0.5),
    vec2(-0.5, -0.5), vec2( 0.5,  0.5), vec2(-0.5,  0.5)
);

void main() {
    vec3 pos = inPosScale.xyz;
    float scale = inPosScale.w;
    vec3 vel = inVelTime.xyz;
    float time = inVelTime.w;

    vec2 vPos = quadVertices[gl_VertexIndex];
    fragUV = vec2(vPos.x + 0.5, 1.0 - (vPos.y + 0.5));

    float randomSeed = fract(sin(float(gl_InstanceIndex) * 12.9898) * 43758.5453);
    vec3 colorWhite = vec3(1.0, 1.0, 1.0);
    vec3 colorPink  = vec3(1.0, 0.75, 0.85); 
    fragColor = mix(colorWhite, colorPink, randomSeed);

    float speed = length(vel);
    
    // 1. Core Flight Orientation
    vec3 flatForward = normalize(vec3(vel.x, 0.0, vel.z) + vec3(0.0001, 0.0, 0.0001));
    vec3 worldUp = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(worldUp, flatForward));

    float pitchAmount = clamp(speed * 0.08 + 0.2, 0.2, 0.7); 
    vec3 up = normalize(worldUp - flatForward * pitchAmount);
    vec3 trueForward = normalize(cross(right, up));

    // 2. Heavy Camera & Vertical Bias
    vec3 toCamera = normalize(ubo.cameraPos.xyz - pos);
    
    // Add an artificial vertical lift (+0.8 on Y) to the camera vector.
    // This makes the math act as if your camera is always hovering slightly 
    // above the butterfly, exposing the top of the wings when they fly at you.
    vec3 elevatedCamera = normalize(toCamera + vec3(0.0, 0.8, 0.0));
    
    // Cranked the bias up to 65% so the camera/vertical lift heavily overpowers the flight pitch
    vec3 finalUp = normalize(mix(up, elevatedCamera, 0.65));
    vec3 finalRight = normalize(cross(finalUp, trueForward));
    vec3 finalForward = normalize(cross(finalRight, finalUp));

    // 3. Harmonic Flapping
    float flapBase = time * 20.0; 
    float flapAmplitude = 0.5 + (speed * 0.05); 
    float flapAngle = (sin(flapBase) * 0.6 + sin(flapBase * 0.4) * 0.3) * flapAmplitude + 0.4; 
    
    vec3 localPos = vec3(0.0);
    
    localPos += finalForward * vPos.y * scale;

    if (vPos.x > 0.0) {
        localPos += (finalRight * cos(flapAngle) + finalUp * sin(flapAngle)) * vPos.x * scale;
    } else {
        localPos += (finalRight * cos(flapAngle) - finalUp * sin(flapAngle)) * vPos.x * scale;
    }

    gl_Position = ubo.proj * ubo.view * vec4(pos + localPos, 1.0);
}