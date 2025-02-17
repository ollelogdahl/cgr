#pragma once

#include "planner.h"

class Renderer {
public:
    // init

    u32 use_texture(ref_t<texture_t> texture);

    void draw(std::span<render_op_t> &render_ops);
};
