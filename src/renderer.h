#pragma once

#include "camera.h"
#include "gpu.h"
#include "resource.h"
#include "oc.h"
#include "linalg.h"

typedef u32 texhnd_t;

struct draw_element_material_t {
    bool use_albedo_tex;
    bool use_normal_tex;
    bool use_roughness_tex;

    union {
        struct {
            f32 color_r, color_g, color_b;
        };
        texhnd_t albedo_tex_idx;
    };

    union {
        f32 roughness;
        texhnd_t roughness_tex_idx;
    };

    f32 metallic;
    texhnd_t normal_tex_idx;
};

struct draw_element_t {
    gpu_buffer_t *vertex_buffer;
    gpu_buffer_t *index_buffer;
    u32 index_count;
    u32 index_start;

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
    void add_element(draw_element_t &element);
    void add_point_light(pl_element_t &element);

    void set_camera(camera_t &camera);

    void new_frame();
    void draw(gpu_t::frame_t &frame);

private:
    gpu_t *gpu;
    ref_t<gpu_pipeline_t> pipeline;
    std::vector<ref_t<texture_t>> defined_textures;

    std::vector<draw_element_t> draw_elements;
    std::vector<pl_element_t> point_lights;

    camera_t *camera;

    VkDescriptorSet descriptor_set;
    VkDescriptorSet texture_descriptor_set;
};
