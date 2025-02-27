#version 450

struct GlobalData {
    mat4 cam_view;
    mat4 cam_proj;
    vec3 cam_pos;
};

struct LODData {
    uint index_start;
    uint index_count;
    float distance;
};

struct MeshData {
    LODData lods[4];
};

// Object data from CPU
struct ObjectData {
    float transform[12];
    uint material_id;
    uint mesh_id;
    uint _pad[2];
};

struct MaterialData {
    vec4 color;
};

layout(set = 0, binding = 0) readonly buffer GlobalBuffer {
    GlobalData global;
};

layout(set = 0, binding = 1) readonly buffer ObjectBuffer {
    ObjectData objects[];
};

layout(set = 0, binding = 2) readonly buffer MaterialBuffer {
    MaterialData materials[];
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec3 vertex_color;

layout(location = 0) out vec3 frag_pos_ws;
layout(location = 1) out vec3 frag_normal_ws;
layout(location = 2) out vec2 frag_uv;
layout(location = 3) out vec3 frag_vertex_color;

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
    gl_Position = global.cam_proj * global.cam_view * wp;

    frag_pos_ws = vec3(wp);
    frag_normal_ws = transpose(inverse(mat3(transform))) * normal;
    frag_uv = vec2(uv.x, -uv.y);
    frag_vertex_color = vertex_color;
}
