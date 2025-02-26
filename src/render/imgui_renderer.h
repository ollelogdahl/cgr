#pragma once

#include "gpu.h"

class ImGuiRenderer {
public:
    ImGuiRenderer(gpu_t &gpu);

    void new_frame();
    void draw(gpu_t::frame_t &frame);
private:
    gpu_t *gpu;
};
