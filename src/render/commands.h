#pragma once

#include "gpu.h"
#include "material.h"

#include <vector>

struct draw_indexed_command_t {
    ref_t<gpu_buffer_t> vertex_buffer;
    ref_t<gpu_buffer_t> index_buffer;
    u32 index_count;
    u32 vertex_offset;
    u32 index_offset;

    m4f transform;
    ref_t<material_t> material;
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

struct commands_t {
    std::vector<draw_indexed_command_t> draw_indexed;
    std::vector<pl_command_t> point_lights;
    std::vector<dl_command_t> directional_lights;

    inline void clear() {
        draw_indexed.clear();
        point_lights.clear();
        directional_lights.clear();
    }
};
