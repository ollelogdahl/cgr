#pragma once

#include <oc.h>
#include <linalg.h>

namespace modimp {

struct mesh_t;
struct material_t;
struct texture_t;

struct scene_t {
    slice<mesh_t> meshes = {};
    slice<material_t> materials = {};
    slice<texture_t> textures = {};
};

result_t<void, std::string> scene_load(scene_t &scene, const char *path);
result_t<void, std::string> scene_load(scene_t &scene, slice<byte> data, const char *hint);
void scene_free(scene_t &scene);

// @todo: figure out ownership. In the obj-loader, each slice refers to a freshly allocated
// array. In the m3d case, it would be better to allocate globally, and refer into it.
struct mesh_t {
    u32 material_index;

    slice<v3f> vertices;
    slice<u32> indices; // note: contains 3 indices per triangle
    slice<v3f> normals;

    aabb_t bounds;

    // optional fields
    // @note: some model representations allow multiple vertex-colors and
    // texcoords per vertex. We will just support one for now.
    slice<u32> colors;
    slice<v2f> texcoords;

    slice<char> name;
};

struct texture_t {
    u32 width;
    u32 height;
    u32 channels;
    byte *data;
};

struct material_t {
    slice<char> name;

    u32 diffuse;
    u32 ambient;
    u32 specular;
    f32 roughness;
    f32 metallic;

    texture_t *tex_diffuse;
    texture_t *tex_normal;
    texture_t *tex_roughness;
};

}
