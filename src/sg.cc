#include "sg.h"

namespace sg {

void scene_t::clear() {
    // delete all nodes.
    nodes.clear();

    // deallocate nodes.
    storage.groups.clear();
    storage.geometries.clear();
    storage.point_lights.clear();
    storage.transforms.clear();
    storage.cameras.clear();
    storage.lods.clear();

    storage.states.clear();
    storage.materials.clear();
}

void lod_t::traverse(node_visitor_t &visitor) {
    // figure out which child we should pick. This is pretty easy.
    // Firstly, we determine the magnitude of center. This is the distance but in local-space.
    // We can assume that all lod:s have the same bounding box.
    // Then we binary search through the ranges to find the correct lod.
    // Then we render that one.
    if (m_children.size() == 0) return;

    assert(m_children.size() == ranges_min.size());

    f32 distance = (center).length();

    // this assumes that the children are sorted ascendingly by lod level.
    // the ranges contains the minimum distance for each lod. The first value
    // is always 0. To show lod[n], the distance must be between ranges[n-1] and ranges[n].
    u32 idx;
    {
        u32 low = 0;
        u32 high = ranges_min.size();
        while (low < high) {
            u32 mid = low + (high - low) / 2;
            if (distance < ranges_min[mid]) {
                high = mid;
            } else {
                low = mid + 1;
            }
        }
        idx = low - 1;
    }

    // fmt::println("lod: {}", idx);
    assert(idx < m_children.size());
    m_children[idx]->accept(visitor);
}

}
