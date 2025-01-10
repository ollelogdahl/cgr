#pragma once

// A simple obj parser.
// The obj parser does not produce meshes. It uses callbacks to announce features inside the
// obj file, such as vertices, normals, texcoords, and faces. These can then be stored
// by the user into whatever data structure they want.

#include "oc.h"
#include "linalg.h"

// @todo: are obj files really valid if some faces have normals and some don't? idk.
struct obj_parser_config_t {
    void (*on_vertex)(void *user_ctx, bool has_color, v4f vertex, v3f color);
    void (*on_normal)(void *user_ctx, v3f normal);
    void (*on_texcoord)(void *user_ctx, v3f texcoord);
    void (*on_material_definition)(void *user_ctx, slice<byte> material_name);
    void (*on_material_use)(void *user_ctx, slice<byte> material_name);
    void (*on_object)(void *user_ctx, slice<byte> object_name);
    void (*on_group)(void *user_ctx, slice<byte> group_name);
    void (*on_smoothing_group)(void *user_ctx, u32 smoothing_group);

    // the index starts at 1. If an index is zero, that property is not present.
    void (*on_face) (void *user_ctx, u32 vertex_n, u32 *vertex_idxs, u32 *texcoord, u32 *normal_idxs);

    void *user_ctx;
};

result_t<void, std::string> parse_obj(slice<byte> obj_contents, obj_parser_config_t config);
