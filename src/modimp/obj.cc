#include "obj.h"
#include "oc.h"

#include <fmt/core.h>

struct face_vertex_t {
    u32 vertex;
    u32 texcoord;
    u32 normal;
};

face_vertex_t next_face_vertex(slice<byte> &content);

result_t<void, std::string> parse_obj(slice<byte> obj_contents, obj_parser_config_t config) {

    auto next_number = [](slice<byte> &content) {
        slice<byte> before;
        content.split(' ', before, content);

        auto v = str2f32(before);

        return v;
    };

    auto cursor = obj_contents;
    while(cursor.len > 0) {
        slice<byte> line;
        slice<byte> word;
        slice<byte> rem;

        cursor.split('\n', line, cursor);
        line.split(' ', word, rem);

        // v x y z [w]
        if(word == S("v")) {
            v4f vertex = {0, 0, 0, 1.0};
            vertex.x = next_number(rem);
            vertex.y = next_number(rem);
            vertex.z = next_number(rem);

            // not too sure about this.
            // if(rem.len > 0) {
            //     vertex.w = next_number(rem);
            // }

            bool has_color = false;
            v3f color;
            if(rem.len > 0) {
                has_color = true;
                color.x = next_number(rem);
                color.y = next_number(rem);
                color.z = next_number(rem);
            }

            if(config.on_vertex) {
                config.on_vertex(config.user_ctx, has_color, vertex, color);
            }
        }
        // vn x y z
        else if(word == S("vn")) {
            auto x = next_number(rem);
            auto y = next_number(rem);
            auto z = next_number(rem);

            if(config.on_normal) {
                config.on_normal(config.user_ctx, {x, y, z});
            }
        }
        // vt u [v] [w]
        else if(word == S("vt")) {
            auto u = next_number(rem);

            f32 v = 0.0f;
            if(rem.len > 0) {
                v = next_number(rem);
            }

            f32 w = 0.0f;
            if(rem.len > 0) {
                w = next_number(rem);
            }

            if(config.on_texcoord) {
                config.on_texcoord(config.user_ctx, {u, v, w});
            }
        }
        // f 1 2 3 ...
        // f 1/1 2/2 3/3 ...
        // f 1/1/1 2/2/2 3/3/3 ...
        // f 1//1 2//2 3//3 ...
        else if(word == S("f")) {
            if(!config.on_face) {
                continue;
            }

            // @todo: should this be configurable?
            const u32 max_vertices = 64;

            u32 vertex_ids[max_vertices];
            u32 texcoord_ids[max_vertices];
            u32 normal_ids[max_vertices];
            u32 vertex_count = 0;

            bool exp_normal = false;
            bool exp_texcoord = false;
            bool is_first = true;
            while(rem.len > 0) {
                // @todo: validation, the flags should be the same for all vertices
                auto v = next_face_vertex(rem);

                bool has_normal = v.normal != 0;
                bool has_texcoord = v.texcoord != 0;
                if(is_first) {
                    exp_normal = has_normal;
                    exp_texcoord = has_texcoord;
                }
                is_first = false;

                if(exp_normal != has_normal || exp_texcoord != has_texcoord) {
                    return fmt::format("inconsistent faces");
                }

                vertex_ids[vertex_count] = v.vertex;
                texcoord_ids[vertex_count] = v.texcoord;
                normal_ids[vertex_count] = v.normal;
                vertex_count++;
                if(vertex_count >= max_vertices) {
                    return fmt::format("too many vertices in single face");
                }
            }

            // @todo: this is validation.
            if(vertex_count >= 3) {
                config.on_face(config.user_ctx, vertex_count, vertex_ids, texcoord_ids, normal_ids);
            }
        }
        else if (word == S("mtllib")) {
            if(config.on_material_definition) {
                config.on_material_definition(config.user_ctx, rem);
            }
        }
        else if (word == S("usemtl")) {
            if(config.on_material_use) {
                config.on_material_use(config.user_ctx, rem);
            }
        }
        else if (word == S("o")) {
            if(config.on_object) {
                config.on_object(config.user_ctx, rem);
            }
        }
        else if (word == S("g")) {
            if(config.on_group) {
                config.on_group(config.user_ctx, rem);
            }
        }
        else if (word == S("s")) {
            if(config.on_smoothing_group) {
                auto v = 0;
                if (rem == S("off")) {}
                else {
                    v = str2u32(rem);
                }

                config.on_smoothing_group(config.user_ctx, v);
            }
        }

    }

    return result_t<void, std::string>::ok();
}

face_vertex_t next_face_vertex(slice<byte> &content) {

    slice<byte> vertex_str;
    content.split(' ', vertex_str, content);

    slice<byte> vertex;
    slice<byte> texcoord;
    slice<byte> normal;

    vertex_str.split('/', vertex, vertex_str);

    u32 v = str2u32(vertex);
    u32 t = 0, n = 0;

    if(vertex_str.len > 0) {
        vertex_str.split('/', texcoord, vertex_str);
        t = str2u32(texcoord);
    }
    if(vertex_str.len > 0) {
        normal = vertex_str;
        n = str2u32(normal);
    }

    return {v, t, n};
}
