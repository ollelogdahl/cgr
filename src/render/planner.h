#pragma once

#include "gpu.h"
#include "material.h"
#include <variant>

typedef u32 texhnd_t;

struct switch_buffers_op_t {
    gpu_buffer_t *vertex_buffer;
    gpu_buffer_t *index_buffer;
};

struct draw_indexed_op_t {
    u32 index_count;
    u32 vertex_offset;
    u32 index_offset;

    m4f transform;
};

struct switch_material_op_t {
    material_t *material;

    texhnd_t albedo0_idx;
    texhnd_t albedo1_idx;
    texhnd_t albedo2_idx;
    texhnd_t normal_idx;
    texhnd_t roughness_idx;
};

typedef std::variant<
    switch_buffers_op_t,
    draw_indexed_op_t,
    switch_material_op_t> render_op_t;

class renderer_t;

class RenderPlanner {
public:
    std::vector<render_op_t> &plan_rendering(renderer_t &renderer);
private:
    std::vector<render_op_t> ops;
};
