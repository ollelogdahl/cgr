#pragma once

#include "camera.h"
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

    void set_camera(camera_t &camera);

    void new_frame();
    void update_frame_data(); // @todo: pass rendering properties here.
    void draw(gpu_t::frame_t &frame);

private:
    friend class RenderPlanner;

    gpu_t *gpu;
    ref_t<gpu_pipeline_t> pipeline;

    texhnd_t get_or_create_texture_handle(ref_t<texture_t> texture);

    struct {
        std::unordered_map<ref_t<texture_t>, texhnd_t> textures = {};
        std::vector<ref_t<texture_t>> slots;
        std::vector<texhnd_t> free_slots;

        bool has_updated = false;
        descriptor_writer_t writer;
    } textures;

    commands_t commands;


    RenderPlanner planner;

    camera_t *camera;

    VkSampler shared_sampler;

    buffer_write_barrier_t ubo_write_barrier;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout main_descriptor_set_layout;
    VkDescriptorSetLayout texture_descriptor_set_layout;
    VkDescriptorSet main_descriptor_set;
    VkDescriptorSet texture_descriptor_set;

    gpu_buffer_t env_ubo_buffer;
};
