#pragma once

#include "linalg.h"
#include "oc.h"
#include <string>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

#include "gpu.h"

#define DECL_IMPL_LOAD_PARAM(type)                                        \
    bool operator==(const type &lhs, const type &rhs);                    \
    template <> struct std::hash<type> {                                  \
        std::size_t operator()(const type &params) const;                 \
    };

struct texture_load_params_t {
    std::string path;
    u32 num_channels = 4;
    bool srgb = true;
};
DECL_IMPL_LOAD_PARAM(texture_load_params_t)

struct model_load_params_t {
    std::string path;

    struct lod_setting_t {
        f32 error_limit;
    };

    std::vector<lod_setting_t> lod_settings = {};
};
DECL_IMPL_LOAD_PARAM(model_load_params_t)

struct texture_t {
    struct {
        u32 width;
        u32 height;
        u32 channels;

        VkFormat format;
    } info;

    gpu_image_t image;
};

struct MeshLOD {
    std::vector<u32> indices;
};

struct Mesh {
    std::vector<v3f> vertices;
    std::vector<v3f> normals;
    std::vector<v2f> uvs;
    std::vector<u32> colors;
    std::vector<MeshLOD> lods;
    aabb_t bounds;
};

struct mesh_description_t {
    ref_t<gpu_buffer_t> vertex_buffer;
    struct lod_t {
        ref_t<gpu_buffer_t> index_buffer;
        u32 index_count;
    };
    std::vector<lod_t> lods;

    aabb_t bounds;
};

struct model_description_t {
    std::vector<mesh_description_t> meshes;
    aabb_t aabb;
};

// used for watching files for changes.
struct fswatcher_t {
    int inotify_fd;

    struct elem_t {
        void (*on_modified)(std::string, void *userdata);
        void *userdata;
        std::string path;

        bool operator==(const elem_t &other) const;
    };

    void init();
    void add_watch(const char *path, void (*on_modified)(std::string, void *userdata), void *userdata);
    void process_watches();

    std::unordered_map<int, elem_t> watches;
};

namespace sg {
class scene_t;
class state_t;
}

struct proc_model_desc_t {
    std::vector<v3f> vertices;
    std::vector<u32> colors = {};
    std::vector<v3f> normals = {};
    std::vector<v2f> uvs = {};
    std::vector<u32> indices;
};

struct loader_t {
    ref_t<gpu_shader_t> load_shader_program(const shader_program_load_params_t &params);

    ref_t<texture_t> load_texture(const texture_load_params_t &params);

    // @todo: loading models is actually really funky. They could contain materials and textures,
    // which we do not support yet.
    model_description_t load_model(const model_load_params_t &params);

    model_description_t build_proc_model(const proc_model_desc_t &desc);

    ref_t<sg::scene_t> load_scene(const char *path);

    void init(gpu_t &gpu);
    void process_hotreload();

    std::vector<std::pair<model_load_params_t, model_description_t>> get_loaded_models() {
        std::vector<std::pair<model_load_params_t, model_description_t>> models;
        for (auto &[params, model] : loaded_models) {
            models.push_back({params, model});
        }
        return models;
    }

private:
    std::unordered_map<shader_program_load_params_t, ref_t<gpu_shader_t>> loaded_shaders;
    std::unordered_map<texture_load_params_t, ref_t<texture_t>> loaded_textures;
    std::unordered_map<model_load_params_t, model_description_t> loaded_models;
    std::unordered_map<std::string, ref_t<sg::scene_t>> loaded_scenes;

    fswatcher_t watcher;
    gpu_t *gpu;

    sg::state_t *m_default_state;

    const char *glslc_path;
};
