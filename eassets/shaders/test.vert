#version 460

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 uv;

layout(binding = 0) uniform Env {
    mat4 cam_view;
    mat4 cam_proj;
} env;

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

    vec4 p = vec4(position, 1.0);
    gl_Position = env.cam_proj * env.cam_view * vec4(position * 0.01, 1.0);
}
