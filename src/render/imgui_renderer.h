#pragma once

#include "gpu.h"

class ImGuiRenderer {
public:
    void init(gpu_t &gpu);

    void new_frame();
    void draw(gpu_t::frame_t &frame);
private:
    gpu_t *gpu;
};
