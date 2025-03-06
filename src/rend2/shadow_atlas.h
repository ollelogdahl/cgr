#pragma once

#include "rend2/render_target.h"
class ShadowAtlas {
public:
    ShadowAtlas(gpu_t &gpu, usize min_entry_width, usize min_entry_height,
        usize num_min_entries_x, usize num_min_entries_y);

    // allow new allocations.
    void reset();

    // allocate a entry with a size factor.
    void allocate(usize size_factor);

    RenderTarget &entry(usize idx);

    VkImageView view() const { return m_view; }
private:

};
