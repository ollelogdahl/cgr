#version 450

struct GlobalData {
    mat4 cam_proj;
    mat4 cam_view;
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
    uint material_id;
    uint mesh_id;
    uint _pad[2];
    mat4x3 transform; // it is actually a 4x3 matrix, but in std140 that takes up the same as mat4.
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

layout(set = 0, binding = 2) buffer MaterialBuffer {
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

void main() {
    ObjectData object = objects[gl_InstanceIndex];

    vec3 wp = object.transform * vec4(position, 1.0);
    gl_Position = global.cam_proj * global.cam_view * vec4(wp, 1.0);


    frag_pos_ws = vec3(wp);
    frag_normal_ws = transpose(inverse(mat3(object.transform))) * normal;
    frag_uv = vec2(uv.x, -uv.y);
    frag_vertex_color = vertex_color;
}
