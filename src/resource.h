#pragma once

#include "oc.h"
#include <string>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

#include "gpu.h"

struct texture_load_params_t {
    const char *path;
};

struct texture_t {
    struct {
        u32 width;
        u32 height;
    } info;

    ref_t<gpu_image_t> image;
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

    void init(gpu_t &gpu);
    void process_hotreload();

    std::unordered_map<shader_program_load_params_t, ref_t<shader_program_t>> loaded_shaders;

    fswatcher_t watcher;
    gpu_t *gpu;
};
