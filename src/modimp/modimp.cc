#include "modimp.h"

#include <vector>
#include <unordered_map>

#include "obj.h"

struct vertex_attribs_t {
    u32 vi, ni, ti;

    bool operator==(const vertex_attribs_t &other) const {
        return vi == other.vi && ni == other.ni && ti == other.ti;
    }
};

template<>
struct std::hash<vertex_attribs_t> {
    std::size_t operator()(const vertex_attribs_t &va) const {
        return std::hash<u32>{}(va.vi) ^ std::hash<u32>{}(va.ni) ^ std::hash<u32>{}(va.ti);
    }
};

namespace modimp {

result_t<void, std::string> scene_load(scene_t &scene, const char *path) {
    auto file_or_error = file_read(path);
    if (file_or_error.is_err()) {
        return file_or_error.unwrap_err();
    }
    auto file = file_or_error.unwrap();

    auto status = scene_load(scene, file.contents, path);
    file_close(file);

    return status;
}

result_t<void, std::string> scene_load_obj(scene_t &scene, slice<byte> data);

result_t<void, std::string> scene_load(scene_t &scene, slice<byte> data, const char *hint) {
    (void)hint;

    // for now, assume all files are obj files
    return scene_load_obj(scene, data);
}

void scene_free(scene_t &scene) {
    (void)scene;
}

result_t<void, std::string> scene_load_obj(scene_t &scene, slice<byte> data) {

    struct ctx_t {
        std::vector<v3f> curr_obj_vertices;
        std::vector<v3f> curr_obj_normals;
        std::vector<v2f> curr_obj_texcoords;
        slice<byte> curr_material;

        std::vector<u32> curr_obj_triangles;

        bool has_normals = false;
        bool has_texcoords = false;

        std::unordered_map<vertex_attribs_t, u32> index_map;
        std::vector<v3f> curr_vertices;
        std::vector<v3f> curr_normals;
        std::vector<v2f> curr_texcoords;
        std::vector<u32> curr_triangles;

        aabb_t curr_bounds = aabb_t{v3f{INFINITY, INFINITY, INFINITY}, v3f{-INFINITY, -INFINITY, -INFINITY}};
    
        std::vector<mesh_t> meshes;

        void produce_mesh() {
            if (curr_obj_vertices.size() == 0) {
                return;
            }

            auto vertices_ptr = new v3f[curr_vertices.size()];
            auto triangles_ptr = new u32[curr_triangles.size()];
            auto normals_ptr = has_normals ? new v3f[curr_normals.size()] : nullptr;
            auto texcoords_ptr = has_texcoords ? new v2f[curr_texcoords.size()] : nullptr;
            
            memcpy(vertices_ptr, curr_vertices.data(), curr_vertices.size() * sizeof(v3f));
            memcpy(triangles_ptr, curr_triangles.data(), curr_triangles.size() * sizeof(u32));
            if (has_normals) memcpy(normals_ptr, curr_normals.data(), curr_normals.size() * sizeof(v3f));
            if (has_texcoords) memcpy(texcoords_ptr, curr_texcoords.data(), curr_texcoords.size() * sizeof(v2f));

            auto vertices = slice<v3f>{vertices_ptr, curr_vertices.size()};
            auto triangles = slice<u32>{triangles_ptr, curr_triangles.size()};
            auto normals = has_normals ? slice<v3f>{normals_ptr, curr_normals.size()} : slice<v3f>{};
            auto texcoords = has_texcoords ? slice<v2f>{texcoords_ptr, curr_texcoords.size()} : slice<v2f>{};

            mesh_t mesh = {
                .vertices = vertices,
                .indices = triangles,
                .bounds = curr_bounds,
                .normals = normals,
                .colors = {},
                .texcoords = {texcoords},
                .name = {},
            };

            meshes.push_back(mesh);

            curr_bounds = aabb_t{v3f{INFINITY, INFINITY, INFINITY}, v3f{-INFINITY, -INFINITY, -INFINITY}};

            index_map.clear();
            curr_vertices.clear();
            curr_normals.clear();
            curr_texcoords.clear();
            curr_triangles.clear();
        }
    } ctx = {};

    auto on_vertex = [](void *user_ctx, bool has_color, v4f vertex, v3f color) {
        auto ctx = (struct ctx_t *)user_ctx;
        auto v = v3f{vertex.x, vertex.y, vertex.z};
        (void)has_color;
        (void)color;

        ctx->curr_obj_vertices.push_back(v);
        ctx->curr_bounds.include(v);
    };

    auto on_normal = [](void *user_ctx, v3f normal) {
        auto ctx = (struct ctx_t *)user_ctx;
        ctx->curr_obj_normals.push_back(normal);
    };

    auto on_texcoord = [](void *user_ctx, v3f texcoord) {
        auto ctx = (struct ctx_t *)user_ctx;
        ctx->curr_obj_texcoords.push_back(v2f{texcoord.x, texcoord.y});
    };

    auto on_material_use = [](void *user_ctx, slice<byte> material_name) {
        auto ctx = (struct ctx_t *)user_ctx;

        ctx->produce_mesh();

        ctx->curr_material = material_name;
    };

    auto on_face = [](void *user_ctx, u32 vertex_n, u32 *vertex_idxs, u32 *texcoord, u32 *normal_idxs) {
        auto ctx = (struct ctx_t *)user_ctx;
        
        // triangulate and make indices shared.
        for (u32 i = 2; i < vertex_n; ++i) {
            u32 vis[3] = { vertex_idxs[0], vertex_idxs[i - 1], vertex_idxs[i] };
            u32 nis[3] = { normal_idxs[0], normal_idxs[i - 1], normal_idxs[i] };
            u32 tis[3] = { texcoord[0], texcoord[i - 1], texcoord[i] };

            for (auto i = 0; i < 3; ++i) {
                if (tis[i] != 0) ctx->has_texcoords = true;
                if (nis[i] != 0) ctx->has_normals = true;

                auto key = vertex_attribs_t{vis[i], nis[i], tis[i]};
                auto it = ctx->index_map.find(key);
                if (it == ctx->index_map.end()) {
                    u32 idx = ctx->curr_vertices.size();
                    ctx->index_map[key] = idx;

                    v3f v = ctx->curr_obj_vertices[vis[i] - 1];
                    v3f n = ctx->has_normals ? ctx->curr_obj_normals[nis[i] - 1] : v3f{};
                    v2f t = ctx->has_texcoords ? ctx->curr_obj_texcoords[tis[i] - 1] : v2f{};

                    ctx->curr_vertices.push_back(v);
                    if (ctx->has_normals) ctx->curr_normals.push_back(n);
                    if (ctx->has_texcoords) ctx->curr_texcoords.push_back(t);

                    ctx->curr_triangles.push_back(idx);
                } else {
                    ctx->curr_triangles.push_back(it->second);
                }
            }
        }
    };

    auto parse_config = obj_parser_config_t{
        .on_vertex = on_vertex,
        .on_normal = on_normal,
        .on_texcoord = on_texcoord,
        .on_material_use = on_material_use,
        .on_face = on_face,
        .user_ctx = &ctx,
    };
    auto result = parse_obj(data, parse_config);
    ctx.produce_mesh();

    auto meshes_ptr = new mesh_t[ctx.meshes.size()];
    memcpy(meshes_ptr, ctx.meshes.data(), ctx.meshes.size() * sizeof(mesh_t));
    scene.meshes = slice<mesh_t>{meshes_ptr, ctx.meshes.size()};

    return result;
}

}