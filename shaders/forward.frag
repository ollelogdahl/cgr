#version 450

layout(location = 0) in vec3 frag_pos_ws;
layout(location = 1) in vec3 normal_ws;
layout(location = 2) in vec2 frag_uv;
layout(location = 3) in vec3 frag_vertex_color;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}
