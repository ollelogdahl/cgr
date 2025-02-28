#include "model.h"
#include "meshoptimizer/meshoptimizer.h"
#include "modimp.h"

#include "log.h"

static logger_t logger = logger_t("modelloader");

std::vector<byte> interleave_compact(const modimp::mesh_t &mesh, u32 &stride) {
    bool has_uv = mesh.texcoords.len > 0;
    bool has_color = mesh.colors.len > 0;

    stride = 3 * sizeof(f32) + 3 * sizeof(f32) + (has_uv ? 2 * sizeof(f32) : 0) + (has_color ? sizeof(u32) : 0);

    std::vector<byte> interleaved(mesh.vertices.len * stride);
    for (usize i = 0; i < mesh.vertices.len; ++i) {
        auto &p = mesh.vertices[i];
        auto &n = mesh.normals[i];

        usize cursor = i * stride;
        memcpy(&interleaved[cursor], &p, sizeof(p));
        cursor += sizeof(p);
        memcpy(&interleaved[cursor], &n, sizeof(n));
        cursor += sizeof(n);

        if (has_uv) {
            auto &t = mesh.texcoords[i];
            memcpy(&interleaved[cursor], &t, sizeof(t));
            cursor += sizeof(t);
        }

        if (has_color) {
            auto &c = mesh.colors[i];
            memcpy(&interleaved[cursor], &c, sizeof(c));
            cursor += sizeof(c);
        }
    }

    return interleaved;
}

Model load_model(const std::string &path, std::span<LODSetting> lod_settings) {

    Model model;

    modimp::scene_t scene;
    auto res = modimp::scene_load(scene, path.c_str());

    aabb_t model_aabb = aabb_t();

    logger.info("loading model: {}", path);

    usize mesh_num = 0;
    for (auto &m : scene.meshes) {
        mesh_num++;
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

        logger.info("    mesh {}: vtx: {}, idx: {}", mesh_num, m.vertices.len, m.indices.len);

        u32 interleaved_stride;
        auto interleaved = interleave_compact(m, interleaved_stride);
        auto vertex_count = m.vertices.len;

        // generate lods.
        usize lod_num = 0;
        for (auto &lod_setting : lod_settings) {
            lod_num++;
            std::vector<u32> lod_indices;
            lod_indices.resize(m.indices.len);

            f32 error;
            usize new_len = meshopt_simplify(
                lod_indices.data(), m.indices.data, m.indices.len,
                (f32 *)interleaved.data(), vertex_count, interleaved_stride,
                0, lod_setting.target_error, 0, &error);

            logger.info("     lod {}: idx: {}, error: {}", lod_num, new_len, error);

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
