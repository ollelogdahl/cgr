#pragma once

#include "sg.h"

class compute_bounds_visitor_t : public sg::node_visitor_t {
public:
    void visit(sg::group_t &group) override;
    void visit(sg::geometry_t &geometry) override;
    void visit(sg::point_light_t &point_light) override;
    void visit(sg::transform_t &transform) override;
    void visit(sg::camera_t &camera) override;
    void visit(sg::lod_t &lod) override;
};
