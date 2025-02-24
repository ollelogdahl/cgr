
struct GlobalData {

};

struct LODData {
    uint index_start;
    uint index_count;
    float distance;
};

struct MeshData {
    LODData lods[4];
};

struct MaterialData {

};

layout(set = 0, binding = 0) readonly buffer GlobalBuffer {
    GlobalData global;
};

layout(set = 0, binding = 1) buffer MeshBuffer {
    MeshData meshes[];
};

layout(set = 0, binding = 2) buffer MaterialBuffer {
    MaterialData materials[];
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inVertexColor;
