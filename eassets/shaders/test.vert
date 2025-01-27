#version 460

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

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

void main() {
    // gl_Position = renderer.cam_proj * renderer.cam_view * vec4(position, 1.0);
    //
    // rotate around z axis
    //float a = 0;
    //mat4 r = mat4(
    //        vec4(cos(a), -sin(a), 0.0, 0.0),
    //        vec4(sin(a), cos(a), 0.0, 0.0),
    //        vec4(0.0, 0.0, 1.0, 0.0),
    //        vec4(0.0, 0.0, 0.0, 1.0)
    //    );

    vec4 p = element.transform * vec4(position, 1.0);

    gl_Position = env.cam_proj * env.cam_view * p;
    frag_pos_ws = vec3(p);
    frag_normal_ws = transpose(inverse(mat3(element.transform))) * normal;
    frag_uv = vec2(uv.x, -uv.y);
}
