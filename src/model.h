#pragma once

#include "linalg.h"
#include <span>

struct MeshLOD {
    std::vector<u32> indices;
    f32 min_distance;
};

struct Mesh {
    std::vector<v3f> vertices;
    std::vector<v3f> normals;
    std::vector<v2f> uvs;
    std::vector<u32> colors;
    std::vector<MeshLOD> lods;
    aabb_t bounds;
};

struct Model {
    std::vector<Mesh> meshes;
    aabb_t aabb;
};

struct LODSetting {
    f32 min_distance;
    f32 target_error;
};

Model load_model(const std::string &path, std::span<LODSetting> lod_settings);
