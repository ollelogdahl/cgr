#pragma once

#include "linalg.h"
#include "oc.h"
#include <string>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

#include "gpu.h"

struct texture_load_params_t {
    const char *path;

    bool operator==(const texture_load_params_t &other) const {
        return strcmp(path, other.path) == 0;
    }
};

template <>
struct std::hash<texture_load_params_t> {
    std::size_t operator()(const texture_load_params_t &params) const {
        return std::hash<const char*>{}(params.path);
    }
};

struct model_load_params_t {
    const char *path;

    struct lod_setting_t {
        f32 distance;
        f32 error_limit;
        bool sloppy = false;
    };

    slice<const lod_setting_t> lod_settings = {};

    bool operator==(const model_load_params_t &other) const {
        return strcmp(path, other.path) == 0;
    }
};

template <>
struct std::hash<model_load_params_t> {
    std::size_t operator()(const model_load_params_t &params) const {
        return std::hash<const char*>{}(params.path);
    }
};

struct texture_t {
    struct {
        u32 width;
        u32 height;
        u32 channels;

        VkFormat format;
    } info;

    gpu_image_t image;
};

struct mesh_t {
    gpu_buffer_t vertex_buffer;
    struct lod_t {
        f32 lod_distance_sq;

        gpu_buffer_t index_buffer;
        u32 index_count;
    };
    std::vector<lod_t> lods;

    aabb_t bounds;
};

struct model_t {
    std::vector<ref_t<mesh_t>> meshes;
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

struct loader_t {
    ref_t<shader_program_t> load_shader_program(const shader_program_load_params_t &params);

    ref_t<texture_t> load_texture(const texture_load_params_t &params);
    ref_t<model_t> load_model(const model_load_params_t &params);

    void init(gpu_t &gpu);
    void process_hotreload();

    std::unordered_map<shader_program_load_params_t, ref_t<shader_program_t>> loaded_shaders;
    std::unordered_map<texture_load_params_t, ref_t<texture_t>> loaded_textures;
    std::unordered_map<model_load_params_t, ref_t<model_t>> loaded_models;

    fswatcher_t watcher;
    gpu_t *gpu;

    const char *glslc_path;
};
