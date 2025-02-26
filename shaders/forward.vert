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
    uint material_id;
    uint mesh_id;
    mat3x4 transform; // transform transposed.
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

mat4 unpack_transform(mat3x4 t) {
    return mat4(
        vec4(t[0].x, t[1].x, t[2].x, 0.0),
        vec4(t[0].y, t[1].y, t[2].y, 0.0),
        vec4(t[0].z, t[1].z, t[2].z, 0.0),
        vec4(t[0].w, t[1].w, t[2].w, 1.0)
    );
}

void main() {
    ObjectData object = objects[gl_InstanceIndex];

    mat4 transform = unpack_transform(object.transform);
    vec4 wp = transform * vec4(position, 1.0);

    gl_Position = global.cam_proj * global.cam_view * wp;


    frag_pos_ws = vec3(wp);
    frag_normal_ws = transpose(inverse(mat3(transform))) * normal;
    frag_uv = vec2(uv.x, -uv.y);
    frag_vertex_color = vertex_color;
}
