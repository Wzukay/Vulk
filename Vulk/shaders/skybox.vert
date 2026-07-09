#version 450

layout(location = 0) out vec2 outUV;

void main() {
    const vec2 pos[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 uv = pos[gl_VertexIndex];
    gl_Position = vec4(uv, 1.0, 1.0);
    outUV = uv; // pass raw clip xy through, no divide/normalize here
}