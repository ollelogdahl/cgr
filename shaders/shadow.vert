#version 450

struct ObjectData {
    float transform[12];
    uint material_id;
    uint mesh_id;
    uint batch_id;
    uint _pad;
};

layout(set = 0, binding = 1) readonly buffer ObjectBuffer {
    ObjectData objects[];
};

layout(push_constant) uniform PushConsts {
    layout(offset = 0) mat4 view_proj;
} global;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec3 vertex_color;

mat4 unpack_affine_transform(uint object_id) {
    float[12] transform = objects[object_id].transform;
    return mat4(
        vec4(transform[0], transform[1], transform[2], 0.0),
        vec4(transform[3], transform[4], transform[5], 0.0),
        vec4(transform[6], transform[7], transform[8], 0.0),
        vec4(transform[9], transform[10], transform[11], 1.0)
    );
}

void main() {
    uint object_id = gl_InstanceIndex;
    mat4 transform = unpack_affine_transform(object_id);
    vec4 wp = transform * vec4(position, 1.0);
    gl_Position = global.view_proj * wp;
}
