#include "modimp.h"

#include <vector>
#include <unordered_map>

#include "linalg.h"
#include "modimp/m3d.h"
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
result_t<void, std::string> scene_load_m3d(scene_t &scene, slice<byte> data);

result_t<void, std::string> scene_load(scene_t &scene, slice<byte> data, const char *hint) {

    if (strstr(hint, ".m3d") != nullptr) {
        return scene_load_m3d(scene, data);
    }

    // for now, assume all files are obj files
    return scene_load_obj(scene, data);
}

void scene_free(scene_t &scene) {
    (void)scene;
}

result_t<void, std::string> scene_load_m3d(scene_t &scene, slice<byte> data) {

    auto read_file_stub = [](char *filename, u32 *size) -> u8 * {
        (void)filename;
        (void)size;
        return nullptr;
    };

    m3d_t *m = m3d_load(data.data, read_file_stub, nullptr, nullptr);
    if (m == nullptr) {
        return std::string("failed to load m3d file");
    }

    // @todo: most likely, the memory model of m3d is wayy better than ours.
    // The way we need to reshuffle the data is really not neccessary (we are not doing
    // any real work).
    //
    // on the other hand, it does not seem like m3d uses a shared index array for
    // vertices. So this might be the way.
    //
    // figure out how we should do this better.

    std::vector<mesh_t> meshes = {};

    std::vector<u32> curr_indices = {};
    std::vector<v3f> curr_vertices = {};
    std::vector<u32> curr_colors = {};
    std::vector<v3f> curr_normals = {};
    std::vector<v2f> curr_texcoords = {};
    aabb_t curr_bounds = aabb_t();

    std::unordered_map<vertex_attribs_t, u32> index_map = {};

    u32 last_materialid = 0;

    auto finish_mesh = [&]() {
        auto indices_ptr = new u32[curr_indices.size()];
        auto vertices_ptr = new v3f[curr_vertices.size()];
        auto normals_ptr = new v3f[curr_normals.size()];
        auto texcoords_ptr = curr_texcoords.size() > 0
            ? new v2f[curr_texcoords.size()]
            : nullptr;
        auto colors_ptr = curr_colors.size() > 0
            ? new u32[curr_colors.size()]
            : nullptr;

        memcpy(indices_ptr, curr_indices.data(), curr_indices.size() * sizeof(u32));
        memcpy(vertices_ptr, curr_vertices.data(), curr_vertices.size() * sizeof(v3f));
        memcpy(normals_ptr, curr_normals.data(), curr_normals.size() * sizeof(v3f));
        if (curr_texcoords.size() > 0) memcpy(texcoords_ptr, curr_texcoords.data(), curr_texcoords.size() * sizeof(v2f));
        if (curr_colors.size() > 0) memcpy(colors_ptr, curr_colors.data(), curr_colors.size() * sizeof(u32));

        auto indices = slice<u32>{indices_ptr, curr_indices.size()};
        auto vertices = slice<v3f>{vertices_ptr, curr_vertices.size()};
        auto normals = slice<v3f>{normals_ptr, curr_normals.size()};
        auto texcoords = curr_texcoords.size() > 0
            ? slice<v2f>{texcoords_ptr, curr_texcoords.size()}
            : slice<v2f>{};
        auto colors = curr_colors.size() > 0
            ? slice<u32>{colors_ptr, curr_colors.size()}
            : slice<u32>{};

        meshes.push_back({
            .material_index = last_materialid,
            .vertices = vertices,
            .indices = indices,
            .normals = normals,
            .bounds = curr_bounds,
            .colors = colors,
            .texcoords = texcoords,
            .name = {},
        });

        curr_indices.clear();
        curr_vertices.clear();
        curr_colors.clear();
        curr_normals.clear();
        curr_texcoords.clear();
        curr_bounds = aabb_t();
    };

    for (u32 i = 0; i < m->numface; ++i) {
        auto &face = m->face[i];
        if (i != 0 && face.materialid != last_materialid) {
            finish_mesh();
        }
        last_materialid = face.materialid;

        v3f gen_normal;
        {
            auto &vx1 = m->vertex[face.vertex[0]];
            auto &vx2 = m->vertex[face.vertex[1]];
            auto &vx3 = m->vertex[face.vertex[2]];
            v3f v1 = v3f{vx1.x, vx1.y, vx1.z};
            v3f v2 = v3f{vx2.x, vx2.y, vx2.z};
            v3f v3 = v3f{vx3.x, vx3.y, vx3.z};

            v3f a = v2 - v1;
            v3f b = v3 - v1;

            gen_normal = v3f::cross(a, b);
        }

        for (u32 j = 0; j < 3; ++j) {
            auto &vi = face.vertex[j];
            auto &ni = face.normal[j];
            auto &ti = face.texcoord[j];

            vertex_attribs_t va = { vi, ni, ti };
            auto it = index_map.find(va);
            if (it == index_map.end()) {
                u32 idx = curr_vertices.size();
                index_map[va] = idx;

                curr_indices.push_back(idx);

                auto &vx = m->vertex[vi];
                auto v = v3f{vx.x, vx.y, vx.z};
                curr_bounds.include(v);

                curr_vertices.push_back(v);
                curr_colors.push_back(vx.color);

                if (ni != -1U) {
                    auto &n = m->vertex[ni];
                    curr_normals.push_back(v3f{n.x, n.y, n.z});
                } else {
                    // generate normal
                    curr_normals.push_back(gen_normal);
                }

                if (ti != -1U) {
                    auto &t = m->tmap[ti];
                    curr_texcoords.push_back(v2f{t.u, t.v});
                }
            } else {
                curr_indices.push_back(it->second);
            }
        }
    }

    if (curr_indices.size() > 0)
        finish_mesh();

    auto meshes_ptr = new mesh_t[meshes.size()];
    memcpy(meshes_ptr, meshes.data(), meshes.size() * sizeof(mesh_t));
    scene.meshes = slice<mesh_t>{meshes_ptr, meshes.size()};

    return {};
}

result_t<void, std::string> scene_load_obj(scene_t &scene, slice<byte> data) {

    struct ctx_t {
        std::vector<v3f> curr_obj_vertices;
        std::vector<u32> curr_obj_vertex_colors;
        std::vector<v3f> curr_obj_normals;
        std::vector<v2f> curr_obj_texcoords;
        slice<byte> curr_material;

        std::vector<u32> curr_obj_triangles;

        bool first_vertex = true;
        bool has_color = false;

        bool has_texcoords = false;
        bool knows_if_has_texcoords = false;

        std::unordered_map<vertex_attribs_t, u32> index_map;
        std::vector<v3f> curr_vertices;
        std::vector<u32> curr_colors;
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
            auto normals_ptr = new v3f[curr_normals.size()];
            auto texcoords_ptr = has_texcoords
                ? new v2f[curr_texcoords.size()]
                : nullptr;
            auto colors_ptr = has_color
                ? new u32[curr_colors.size()]
                : nullptr;

            memcpy(vertices_ptr, curr_vertices.data(), curr_vertices.size() * sizeof(v3f));
            memcpy(triangles_ptr, curr_triangles.data(), curr_triangles.size() * sizeof(u32));
            memcpy(normals_ptr, curr_normals.data(), curr_normals.size() * sizeof(v3f));

            if (has_texcoords) memcpy(texcoords_ptr, curr_texcoords.data(), curr_texcoords.size() * sizeof(v2f));
            if (has_color) memcpy(colors_ptr, curr_colors.data(), curr_colors.size() * sizeof(v3f));

            auto vertices = slice<v3f>{vertices_ptr, curr_vertices.size()};
            auto triangles = slice<u32>{triangles_ptr, curr_triangles.size()};
            auto normals = slice<v3f>{normals_ptr, curr_normals.size()};

            auto texcoords = has_texcoords
                ? slice<v2f>{texcoords_ptr, curr_texcoords.size()}
                : slice<v2f>{};
            auto colors = has_color
                ? slice<u32>{colors_ptr, curr_colors.size()}
                : slice<u32>{};

            mesh_t mesh = {
                .material_index = 0,
                .vertices = vertices,
                .indices = triangles,
                .normals = normals,
                .bounds = curr_bounds,
                .colors = { colors },
                .texcoords = {texcoords},
                .name = {},
            };

            meshes.push_back(mesh);

            curr_bounds = aabb_t{v3f{INFINITY, INFINITY, INFINITY}, v3f{-INFINITY, -INFINITY, -INFINITY}};

            index_map.clear();
            curr_vertices.clear();
            curr_colors.clear();
            curr_normals.clear();
            curr_texcoords.clear();
            curr_triangles.clear();

            knows_if_has_texcoords = false;
            first_vertex = true;
        }
    } ctx = {};

    auto on_vertex = [](void *user_ctx, bool has_color, v4f vertex, v3f color) {
        auto ctx = (struct ctx_t *)user_ctx;
        auto v = v3f{vertex.x, vertex.y, vertex.z};

        if (ctx->first_vertex) ctx->has_color = has_color;
        ctx->first_vertex = false;

        ctx->curr_obj_vertices.push_back(v);
        if (has_color) {
            // decode it as a u32 r8g8b8a8
            // alpha is fixed to 255.
            u8 r = (u8)(color.x * 255.0f);
            u8 g = (u8)(color.y * 255.0f);
            u8 b = (u8)(color.z * 255.0f);
            u32 rgba = (r << 24) | (g << 16) | (b << 8) | 0xff;

            ctx->curr_obj_vertex_colors.push_back(rgba);
        }

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

            auto gen_normals = [&](u32 vi[3]) {
                v3f p0 = ctx->curr_obj_vertices[vi[0] - 1];
                v3f p1 = ctx->curr_obj_vertices[vi[1] - 1];
                v3f p2 = ctx->curr_obj_vertices[vi[2] - 1];

                v3f v0 = p1 - p0;
                v3f v1 = p2 - p0;
                return v3f::normalize(v3f::cross(v0, v1));
            };

            // i guess we should only generate normals if the user wants that.
            // Not sure how to handle this, nor do i care.
            v3f gen_normal = gen_normals(vis);

            for (auto i = 0; i < 3; ++i) {
                bool has_texcoords = tis[i] != 0;
                bool defined_normals = nis[i] != 0;

                if (!ctx->knows_if_has_texcoords) {
                    ctx->has_texcoords = has_texcoords;
                    ctx->knows_if_has_texcoords = true;
                }

                auto key = vertex_attribs_t{vis[i], nis[i], tis[i]};
                auto it = ctx->index_map.find(key);
                if (it == ctx->index_map.end()) {
                    u32 idx = ctx->curr_vertices.size();
                    ctx->index_map[key] = idx;

                    v3f v = ctx->curr_obj_vertices[vis[i] - 1];

                    u32 c = ctx->has_color
                        ? ctx->curr_obj_vertex_colors[vis[i] - 1]
                        : 0xffffffff;

                    v3f n = defined_normals
                        ? ctx->curr_obj_normals[nis[i] - 1]
                        : gen_normal;

                    v2f t = has_texcoords
                        ? ctx->curr_obj_texcoords[tis[i] - 1]
                        : v2f{};

                    ctx->curr_vertices.push_back(v);
                    ctx->curr_normals.push_back(n);

                    if (ctx->has_color) ctx->curr_colors.push_back(c);
                    if (has_texcoords) ctx->curr_texcoords.push_back(t);

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
        .on_material_definition = nullptr,
        .on_material_use = on_material_use,
        .on_object = nullptr,
        .on_group = nullptr,
        .on_smoothing_group = nullptr,
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
