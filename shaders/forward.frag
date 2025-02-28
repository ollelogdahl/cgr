#version 450

layout(location = 0) in vec3 frag_pos_ws;
layout(location = 1) in vec3 frag_normal_ws;
layout(location = 2) in vec2 frag_uv;
layout(location = 3) in vec3 frag_vertex_color;
layout(location = 4) in flat uint material_id;

layout(location = 0) out vec4 outColor;

struct MaterialData {
    vec4 color;
    vec4 emission;
};

layout(set = 0, binding = 2) readonly buffer MaterialBuffer {
    MaterialData materials[];
};

void main() {
    outColor = materials[material_id].color;
}
