#version 460

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 3) in vec3 vertex_color;

layout(binding = 0) uniform Env {
    mat4 cam_view;
    mat4 cam_proj;
    vec3 cam_pos;
} env;

layout(push_constant) uniform PushConsts {
    layout(offset = 0) mat4 transform;
} element;

layout(location = 0) out vec3 frag_pos_ws;
layout(location = 1) out vec3 frag_normal_ws;
layout(location = 2) out vec2 frag_uv;
layout(location = 3) out vec3 frag_vertex_color;

void main() {
    vec4 p = element.transform * vec4(position, 1.0);

    gl_Position = env.cam_proj * env.cam_view * p;
    frag_pos_ws = vec3(p);
    frag_normal_ws = transpose(inverse(mat3(element.transform))) * normal;
    frag_uv = vec2(uv.x, -uv.y);
    frag_vertex_color = vertex_color;
}
