#include "bounds_visitor.h"

void compute_bounds_visitor_t::visit(sg::group_t &group) {
    group.accept_children(*this);

    aabb_t aabb;
    for (auto &child : group.children()) {
        aabb.include(child->bounding_box());
    }
    group.set_bounding_box(aabb);
}
void compute_bounds_visitor_t::visit(sg::geometry_t &geometry) {
    (void)geometry;
    // we assume that geometries already have their bounding box set.
}
void compute_bounds_visitor_t::visit(sg::point_light_t &point_light) {
    (void)point_light;
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
void compute_bounds_visitor_t::visit(sg::camera_t &camera) { (void)camera; }
void compute_bounds_visitor_t::visit(sg::lod_t &lod) {
    lod.accept_children(*this);
    lod.set_bounding_box(lod.children()[0]->bounding_box());
}
