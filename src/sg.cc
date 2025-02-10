#include "sg.h"

namespace sg {

void lod_t::traverse(node_visitor_t &visitor) {
    // figure out which child we should pick. This is pretty easy.
    // Firstly, we determine the distance from the camera to the center of the bounding box.
    // We can assume that all lod:s have the same bounding box.
    // Then we binary search through the ranges to find the correct lod.
    // Then we render that one.
    if (children.size() == 0) return;

    assert(children.size() == ranges_min.size());

    f32 distance = (center - children[0]->bounding_box().center()).length();

    // this assumes that the children are sorted ascendingly by lod level.
    u32 idx = 0;
    {
        u32 low = 0;
        u32 high = ranges_min.size();
        while (low < high) {
            u32 mid = low + (high - low) / 2;
            if (ranges_min[mid] < distance) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
    }

    fmt::println("lod: {}", idx);

    assert(idx < children.size());
    children[idx]->accept(visitor);
}

}
