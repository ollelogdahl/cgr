#include "bounds_visitor.h"

void compute_bounds_visitor_t::visit(sg::group_t &group) {
    group.accept_children(*this);

    aabb_t aabb;
    for (auto &child : group.children()) {
        aabb.include(child->bounding_box());
    }
    group.set_bounding_box(aabb);
}
void compute_bounds_visitor_t::visit(sg::transform_t &transform) {
    transform.accept_children(*this);

    aabb_t aabb;
    for (auto &child : transform.children()) {
        aabb.include(
            child->bounding_box().transform_affine(transform.get_local_matrix()));
    }
    transform.set_bounding_box(aabb);
}
