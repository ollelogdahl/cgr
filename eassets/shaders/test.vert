#version 460

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 uv;

/*
layout(binding = 0) uniform Renderer {
    mat4 cam_view;
    mat4 cam_proj;
} renderer;
*/

void main() {
    // gl_Position = renderer.cam_proj * renderer.cam_view * vec4(position, 1.0);
    gl_Position = vec4(position * 0.5, 1.0);
}
