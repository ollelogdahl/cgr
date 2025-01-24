#pragma once

#include "gpu.h"
#include "resource.h"
#include "oc.h"
#include "linalg.h"

struct draw_element_material_t {
    f32 color_r, color_g, color_b;
    f32 roughness;
    f32 metallic;

    f32 _padding[3] = {0};
};

struct draw_element_t {
    gpu_buffer_t *vertex_buffer;
    gpu_buffer_t *index_buffer;
    u32 index_count;
    u32 index_start;

    m4f transform;
    draw_element_material_t *material;
};

class renderer_t {
public:
    void init(gpu_t &gpu);

    void add_element(draw_element_t &element);

    void new_frame();
    void draw(gpu_t::frame_t &frame);
private:
    ref_t<gpu_pipeline_t> pipeline;
};
