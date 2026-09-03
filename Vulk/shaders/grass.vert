#version 450

// FIX: Replaced physical vertex inputs with a Storage Buffer fetch mechanism
struct GrassInstance {
    vec3 position;
    float rotation;
    vec3 scale;
    float windOffset;
};

// Set 1 corresponds to `m_globalComputeSets`. Binding 1 is `m_culledBuffers`.
layout(set = 1, binding = 1) readonly buffer CulledInstances {
    GrassInstance instances[];
};

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragNormal;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view; mat4 proj; vec3 cameraPos; float ambient;
    vec4 fadeParams; vec2 screenSize; float specularPower; uint lightCount;
    float fogStart; float fogEnd; vec4 sunDirection; vec4 sunColor;
    mat4 inverseViewProj; mat4 inverseProj; mat4 inverseView;
} ubo;

layout(push_constant) uniform PushConstants {
    float time; uint textureId; float windStrength; float windSpeed; float lodFactor;
} pc;

vec3 EvaluateBezier(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float u = 1.0 - t;
    return (u*u*u)*p0 + 3.0*(u*u)*t*p1 + 3.0*u*(t*t)*p2 + (t*t*t)*p3;
}

vec3 EvaluateBezierDerivative(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float u = 1.0 - t;
    return 3.0*u*u*(p1 - p0) + 6.0*u*t*(p2 - p1) + 3.0*t*t*(p3 - p2);
}

void main() {
    // FIX: Programmable Vertex Pulling! Grab the specific instance for this draw call
    GrassInstance inst = instances[gl_InstanceIndex];
    vec3 inPos = inst.position;
    float inRot = inst.rotation;
    vec3 inScale = inst.scale;
    float inWindOff = inst.windOffset;

    float dist = distance(inPos, ubo.cameraPos);
    
    int lodLevel = 0;
    if (dist > ubo.fadeParams.w * 0.6) {
        lodLevel = 2; // Far: 3 verts
    } else if (dist > ubo.fadeParams.w * 0.25) {
        lodLevel = 1; // Mid: 9 verts
    }

    int activeTriangles = (lodLevel == 0) ? 5 : ((lodLevel == 1) ? 3 : 1);
    int tri = gl_VertexIndex / 3;
    int v = gl_VertexIndex % 3;

    if (tri >= activeTriangles) {
        gl_Position = vec4(0.0);
        return;
    }

    bool isTipTri = (tri == activeTriangles - 1);
    int segment = tri / 2;
    int totalSegments = (activeTriangles + 1) / 2;

    float y0 = float(segment) / float(totalSegments);
    float y1 = float(segment + 1) / float(totalSegments);

    float w0 = 1.0 - pow(y0, 1.5); 
    float w1 = 1.0 - pow(y1, 1.5);

    vec2 uv;
    if (isTipTri) {
        if (v == 0) uv = vec2(-w0, y0);
        else if (v == 1) uv = vec2(w0, y0);
        else uv = vec2(0.0, y1);
    } else {
        bool isSecondTri = (tri % 2 == 1);
        if (!isSecondTri) {
            if (v == 0) uv = vec2(-w0, y0);
            else if (v == 1) uv = vec2(w0, y0);
            else uv = vec2(-w1, y1);
        } else {
            if (v == 0) uv = vec2(w0, y0);
            else if (v == 1) uv = vec2(w1, y1);
            else uv = vec2(-w1, y1);
        }
    }

    float widthOffset = uv.x;
    float t = uv.y; 
    
    fragUV = vec2(widthOffset * 0.5 + 0.5, t);

    float finalRot = inRot;
    if (lodLevel == 2) {
        vec3 toCam = normalize(ubo.cameraPos - inPos);
        finalRot = atan(toCam.x, toCam.z);
    }

    float c = cos(finalRot);
    float s = sin(finalRot);
    vec3 sideDir = vec3(c, 0.0, s); 
    vec3 forwardDir = vec3(-s, 0.0, c); 

    float bladeHeight = inScale.y;
    vec3 p0 = inPos;
    vec3 p1 = p0 + vec3(0.0, bladeHeight * 0.3, 0.0);
    vec3 naturalLean = forwardDir * 0.3 * bladeHeight;
    
    // Define your exact desired wind direction (e.g., blowing towards positive X and Z)
    vec3 windDir = normalize(vec3(0.8, 0.0, 0.6)); // Adjust these numbers to change the global direction

    // Global world-space frequency makes the wind sweep across clumps uniformly
    float spatialFrequency = 0.05; 
    float windWave = sin(pc.time * pc.windSpeed + (inPos.x * windDir.x + inPos.z * windDir.z) * spatialFrequency + inWindOff);

    vec3 windPush = windDir * (windWave * pc.windStrength * bladeHeight * 0.5);

    vec3 p2 = p0 + vec3(0.0, bladeHeight * 0.7, 0.0) + naturalLean + windPush * 0.5;
    vec3 p3 = p0 + vec3(0.0, bladeHeight, 0.0) + naturalLean * 1.5 + windPush;
    
    float bendDist = length(p3.xz - p0.xz);
    p3.y -= bendDist * 0.55; 
    p2.y -= bendDist * 0.15;

    vec3 spinePos = EvaluateBezier(p0, p1, p2, p3, t);
    vec3 curveTangent = normalize(EvaluateBezierDerivative(p0, p1, p2, p3, t));

    vec3 worldPos = spinePos + (sideDir * widthOffset * inScale.x * 0.4);

    vec3 surfaceNormal = normalize(cross(sideDir, curveTangent));
    vec3 tiltedNormal = normalize(surfaceNormal * 0.6 + vec3(0.0, 0.8, 0.0));
    float fadeLerp = clamp((dist - ubo.fadeParams.z) / (ubo.fadeParams.w - ubo.fadeParams.z), 0.0, 1.0);
    fragNormal = normalize(mix(tiltedNormal, vec3(0.0, 1.0, 0.0), fadeLerp));

    vec3 viewDir = normalize(ubo.cameraPos - inPos);
    vec3 pushDir = viewDir;
    pushDir.y = 0.0;
    if (length(pushDir) > 0.001) { pushDir = normalize(pushDir); } 
    else { pushDir = vec3(1.0, 0.0, 0.0); }
    
    float topDownFactor = max(viewDir.y, 0.0);
    vec3 skewOffset = pushDir * (t * t) * topDownFactor * (bladeHeight * 0.35); 
    worldPos += skewOffset;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
}