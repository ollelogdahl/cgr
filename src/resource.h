#pragma once

#include "oc.h"
#include <string>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

#include "gpu.h"

struct shader_program_t;

struct shader_program_load_params_t {
    const char *vertex_hlsl_path;
    const char *fragment_hlsl_path;

    bool operator==(const shader_program_load_params_t &other) const {
        return strcmp(vertex_hlsl_path, other.vertex_hlsl_path) == 0 &&
               strcmp(fragment_hlsl_path, other.fragment_hlsl_path) == 0;
    }
};

struct pipeline_config_t {
    ref_t<shader_program_t> shader;

    // @todo: make this cleaner and make optional
    struct {
        slice<const VkVertexInputBindingDescription> bindings;
        slice<const VkVertexInputAttributeDescription> attributes;
    } vertex_input_info;

    struct {
        bool depth_test;
        bool depth_write;
        VkCompareOp depth_compare_op;
    } depth_stencil;

    VkPipelineMultisampleStateCreateInfo multisampling;
    VkPipelineLayout pipeline_layout;

    slice<const VkFormat> color_attachment_formats;
    VkFormat depth_attachment_format;

    bool operator ==(const pipeline_config_t &other) const {
        // @todo: fix this up when the config is done.
        return shader == other.shader;
    }
};

struct shader_program_t {
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    shader_program_load_params_t params;
    bool modified = false;
};

struct gpu_pipeline_t {
    VkPipeline pipeline;
    VkPipelineLayout layout;

    pipeline_config_t config;

    std::vector<VkFormat> color_attachment_formats;


    struct {
        std::vector<VkVertexInputBindingDescription> bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;
    } vertex_input_info;

    bool modified = false;
};

template <>
struct std::hash<shader_program_load_params_t> {
    std::size_t operator()(const shader_program_load_params_t &params) const {
        std::size_t h1 = std::hash<const char*>{}(params.vertex_hlsl_path);
        std::size_t h2 = std::hash<const char*>{}(params.fragment_hlsl_path);
        return h1 ^ (h2 << 1);
    }
};

template <>
struct std::hash<pipeline_config_t> {
    std::size_t operator()(const pipeline_config_t &config) const {
        std::size_t h1 = std::hash<shader_program_load_params_t>{}(config.shader->params);
        return h1;
    }
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

    // @todo: I am not a fan of the fact that pipeline creation is a part of the resource loader.
    // In my opinion, it should be a part of the gpu. The issue is that hotreloading requires
    // reconstructing pipelines. It should be easy to move it.
    ref_t<gpu_pipeline_t> make_pipeline(const pipeline_config_t &config);

    void init(gpu_t &gpu);
    void process_hotreload();

    std::unordered_map<shader_program_load_params_t, ref_t<shader_program_t>> loaded_shaders;
    std::unordered_map<pipeline_config_t, ref_t<gpu_pipeline_t>> loaded_pipelines;

    fswatcher_t watcher;
    gpu_t *gpu;
};
