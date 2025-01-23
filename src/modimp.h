#pragma once

#include <oc.h>
#include <linalg.h>

namespace modimp {

#define MODIMP_MAX_COLOR_SETS 1
#define MODIMP_MAX_TEXCOORD_SETS 1

struct mesh_t;
struct material_t;

struct scene_t {
    slice<mesh_t> meshes = {};
    slice<material_t> materials = {};
};

result_t<void, std::string> scene_load(scene_t &scene, const char *path);
result_t<void, std::string> scene_load(scene_t &scene, slice<byte> data, const char *hint);
void scene_free(scene_t &scene);

// @todo: could also do with a slice of faces accessor. This is not done now
// for performance reasons; we always triangulate meshes.
struct mesh_t {
    u32 material_index;

    slice<v3f> vertices;
    slice<u32> indices; // note: contains 3 indices per triangle

    aabb_t bounds;

    // optional fields
    slice<v3f> normals;
    slice<v4f> colors[MODIMP_MAX_COLOR_SETS];
    slice<v2f> texcoords[MODIMP_MAX_TEXCOORD_SETS];

    slice<char> name;
};

struct material_t {
    slice<char> name;
};

}
