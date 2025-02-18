#pragma once

#include "gpu.h"
#include "resource.h"
#include "oc.h"
#include "linalg.h"
#include <vulkan/vulkan_core.h>

#include "render/commands.h"
#include "render/planner.h"

class renderer_t {
public:
    void init(gpu_t &gpu, loader_t &loader);

    // adds a draw element to be rendered in the next frame.
    void add_draw_indexed(const draw_indexed_command_t &cmd);
    void add_point_light(const pl_command_t &cmd);
    void add_directional_light(const dl_command_t &cmd);

    void set_view(const v3f &view_pos, const m4f &view);

    // @todo: redesign this!
    void set_projection(const m4f &projection);

    void new_frame();
    void prepare_drawing();
    void draw(gpu_t::frame_t &frame);

    struct DrawMetrics {
        usize draw_calls = 0;
        usize vertices = 0;
    };

    DrawMetrics last_metrics() {
        return m_last_metrics;
    }

private:
    friend class RenderPlanner;

    gpu_t *gpu;

    // @todo: figure out how to unload these.
    texhnd_t get_or_create_texture_handle(ref_t<texture_t> texture);
    gpu_pipeline_t *get_or_create_pipeline(material_t &material);

    struct {
        std::unordered_map<ref_t<texture_t>, texhnd_t> textures = {};
        std::vector<ref_t<texture_t>> slots;
        std::vector<texhnd_t> free_slots;

        bool has_updated = false;
        descriptor_writer_t writer;
    } textures;

    commands_t commands;
    std::vector<render_op_t> ops;

    RenderPlanner planner;

    m4f view_matrix;
    m4f projection_matrix;
    v3f view_position;

    VkSampler shared_sampler;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout main_descriptor_set_layout;
    VkDescriptorSetLayout texture_descriptor_set_layout;
    VkDescriptorSet main_descriptor_set;
    VkDescriptorSet texture_descriptor_set;

    gpu_buffer_t env_ubo_buffer;

    DrawMetrics m_last_metrics;
};
