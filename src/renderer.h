#pragma once

#include "camera.h"
#include "gpu.h"
#include "resource.h"
#include "oc.h"
#include "linalg.h"
#include <vulkan/vulkan_core.h>

typedef u32 texhnd_t;

struct draw_material_t {
    v4f diffuse;
    v4f ambient = diffuse;
    v4f specular = diffuse;
    f32 roughness;
    f32 metallic;

    ref_t<texture_t> tex_albedo0 = nullptr;
    ref_t<texture_t> tex_albedo1 = nullptr;
    ref_t<texture_t> tex_albedo2 = nullptr;

    ref_t<texture_t> tex_normal = nullptr;
    ref_t<texture_t> tex_roughness = nullptr;

    // @note: these are not to be set by the user.
    texhnd_t albedo0_idx = -1U;
    texhnd_t albedo1_idx = -1U;
    texhnd_t albedo2_idx = -1U;

    texhnd_t normal_idx = -1U;
    texhnd_t roughness_idx = -1U;

    friend class renderer_t;
};

struct draw_indexed_command_t {
    ref_t<gpu_buffer_t> vertex_buffer;
    ref_t<gpu_buffer_t> index_buffer;
    u32 index_count;
    u32 vertex_offset;
    u32 index_offset;

    m4f transform;
    draw_material_t material;
};

struct dl_command_t {
    v3f direction;
    v3f color;
};

struct pl_command_t {
    v3f position;
    v3f color;

    f32 linear;
    f32 quadratic;
};

class renderer_t {
public:
    void init(gpu_t &gpu, loader_t &loader);

    // adds a draw element to be rendered in the next frame.
    void add_draw_indexed(draw_indexed_command_t cmd);
    void add_point_light(const pl_command_t &cmd);
    void add_directional_light(const dl_command_t &cmd);

    void set_camera(camera_t &camera);

    void new_frame();
    void update_frame_data(); // @todo: pass rendering properties here.
    void draw(gpu_t::frame_t &frame);

private:
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

    std::vector<draw_indexed_command_t> draw_elements;
    std::vector<pl_command_t> point_lights;
    std::vector<dl_command_t> directional_lights;

    camera_t *camera;

    VkSampler shared_sampler;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout main_descriptor_set_layout;
    VkDescriptorSetLayout texture_descriptor_set_layout;
    VkDescriptorSet main_descriptor_set;
    VkDescriptorSet texture_descriptor_set;

    gpu_buffer_t env_ubo_buffer;
};
