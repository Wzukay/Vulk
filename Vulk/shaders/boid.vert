#version 450
#include "common_structures.glsl"

layout(location = 0) in vec4 inPosScale;
layout(location = 1) in vec4 inVelTime;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragColor; 

layout(push_constant) uniform PushConstants {
    uint textureId;
    uint animationType;
    vec2 pad;
    vec4 color1;
    vec4 color2;
} pc;

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
    fragColor = mix(pc.color1.rgb, pc.color2.rgb, randomSeed);

    vec3 finalUp = vec3(0.0, 1.0, 0.0);
    vec3 finalRight = vec3(1.0, 0.0, 0.0);
    vec3 finalForward = vec3(0.0, 0.0, 1.0);
    vec3 localPos = vec3(0.0);

    if (pc.animationType == 0) {
        // Butterfly Flapping Logic
        float speed = length(vel);
        vec3 flatForward = normalize(vec3(vel.x, 0.0, vel.z) + vec3(0.0001, 0.0, 0.0001));
        vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), flatForward));

        float pitchAmount = clamp(speed * 0.08 + 0.2, 0.2, 0.7); 
        vec3 up = normalize(vec3(0.0, 1.0, 0.0) - flatForward * pitchAmount);
        vec3 trueForward = normalize(cross(right, up));

        vec3 toCamera = normalize(ubo.cameraPos.xyz - pos);
        vec3 elevatedCamera = normalize(toCamera + vec3(0.0, 0.8, 0.0));
        
        finalUp = normalize(mix(up, elevatedCamera, 0.65));
        finalRight = normalize(cross(finalUp, trueForward));
        finalForward = normalize(cross(finalRight, finalUp));

        float flapBase = time * 20.0; 
        float flapAmplitude = 0.5 + (speed * 0.05); 
        float flapAngle = (sin(flapBase) * 0.6 + sin(flapBase * 0.4) * 0.3) * flapAmplitude + 0.4; 
        
        localPos += finalForward * vPos.y * scale;
        if (vPos.x > 0.0) {
            localPos += (finalRight * cos(flapAngle) + finalUp * sin(flapAngle)) * vPos.x * scale;
        } else {
            localPos += (finalRight * cos(flapAngle) - finalUp * sin(flapAngle)) * vPos.x * scale;
        }
    } else {
        // Firefly Hovering Billboard Logic
        vec3 toCamera = normalize(ubo.cameraPos.xyz - pos);
        finalForward = toCamera;
        finalRight = normalize(cross(vec3(0.0, 1.0, 0.0), finalForward));
        finalUp = cross(finalForward, finalRight);

        // Smooth floating offset
        float hoverX = sin(time * 3.0 + randomSeed * 10.0) * 0.1;
        float hoverY = cos(time * 2.5 + randomSeed * 10.0) * 0.1;
        pos += vec3(hoverX, hoverY, 0.0);

        localPos = (finalRight * vPos.x + finalUp * vPos.y) * scale;
    }

    gl_Position = ubo.proj * ubo.view * vec4(pos + localPos, 1.0);
}