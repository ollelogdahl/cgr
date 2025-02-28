#include "model.h"
#include "meshoptimizer/meshoptimizer.h"
#include "modimp.h"

Model load_model(const std::string &path, std::span<LODSetting> lod_settings) {

    Model model;

    modimp::scene_t scene;
    auto res = modimp::scene_load(scene, path.c_str());

    aabb_t model_aabb = aabb_t();
    for (auto &m : scene.meshes) {
        model_aabb.include(m.bounds.min);
        model_aabb.include(m.bounds.max);

        Mesh mesh;

        mesh.vertices = std::vector<v3f>(m.vertices.begin(), m.vertices.end());
        mesh.normals = std::vector<v3f>(m.normals.begin(), m.normals.end());
        mesh.uvs = std::vector<v2f>(m.texcoords.begin(), m.texcoords.end());
        mesh.colors = std::vector<u32>(m.colors.begin(), m.colors.end());
        mesh.bounds = m.bounds;

        // generate base model (keep = 1.0, min_distance = 0.0)
        {
            mesh.lods.push_back(MeshLOD{.indices = std::vector<u32>(m.indices.begin(), m.indices.end()), .min_distance = 0.0});
        }

        // generate lods.
        for (auto &lod_setting : lod_settings) {
            std::vector<u32> lod_indices;
            lod_indices.reserve(m.indices.len);

            usize target_count = lod_setting.keep_ratio * m.indices.len;
            f32 error;
            usize new_len = meshopt_simplify(
                lod_indices.data(), m.indices.data, m.indices.len,
                (f32 *)m.vertices.data, m.vertices.len, sizeof(v3f),
                target_count, FLT_MAX, 0, &error);

            lod_indices.resize(new_len);
            lod_indices.shrink_to_fit();

            mesh.lods.push_back(
                {.indices = std::move(lod_indices), .min_distance = lod_setting.min_distance}
            );
        }

        model.meshes.push_back(mesh);
    }

    model.aabb = model_aabb;

    return model;
}
