#pragma once

#include "camera.h"
#include "gpu.h"
#include "resource.h"
#include "oc.h"
#include "linalg.h"
#include <vulkan/vulkan_core.h>

typedef u32 texhnd_t;

enum struct draw_element_flags_t {
    none = 0,
    use_albedo_tex = 1 << 0,
    use_normal_tex = 1 << 1,
    use_roughness_tex = 1 << 2,
    use_multi_tex = 1 << 3,
};

struct draw_element_material_t {
    draw_element_flags_t flags;

    v3f color;
    f32 roughness;
    f32 metallic;

    texhnd_t albedo0_idx;
    texhnd_t albedo1_idx;
    texhnd_t albedo2_idx;

    texhnd_t normal_idx;
    texhnd_t roughness_idx;
};

struct draw_indexed_element_t {
    ref_t<gpu_buffer_t> vertex_buffer;
    ref_t<gpu_buffer_t> index_buffer;
    u32 index_count;
    u32 vertex_offset;
    u32 index_offset;

    m4f transform;
    draw_element_material_t material;
};

struct pl_element_t {
    v3f position;
    v3f color;

    f32 linear;
    f32 quadratic;
};

class renderer_t {
public:
    void init(gpu_t &gpu, loader_t &loader);

    // these apply globally to all frames.
    texhnd_t define_texture(ref_t<texture_t> texture);

    // adds a draw element to be rendered in the next frame.
    void add_draw_indexed(const draw_indexed_element_t &element);
    void add_point_light(const pl_element_t &element);

    void set_camera(camera_t &camera);

    void new_frame();
    void update_frame_data(); // @todo: pass rendering properties here.
    void draw(gpu_t::frame_t &frame);

private:
    gpu_t *gpu;
    ref_t<gpu_pipeline_t> pipeline;
    std::vector<ref_t<texture_t>> defined_textures;

    std::vector<draw_indexed_element_t> draw_elements;
    std::vector<pl_element_t> point_lights;

    camera_t *camera;

    VkSampler shared_sampler;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout main_descriptor_set_layout;
    VkDescriptorSetLayout texture_descriptor_set_layout;
    VkDescriptorSet main_descriptor_set;
    VkDescriptorSet texture_descriptor_set;

    gpu_buffer_t env_ubo_buffer;
};
