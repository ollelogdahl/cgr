#pragma once

#include "sg.h"

class update_visitor_t : public sg::node_visitor_t {
public:
    void visit(sg::geometry_t &geometry) override {
        geometry.update();
    }
    void visit(sg::point_light_t &point_light) override {
        point_light.update();
    }
    void visit(sg::camera_t &camera) override {
        camera.update();
    }
    void visit(sg::group_t &group) override {
        for (auto &child : group.children()) {
            child->accept(*this);
        }
        group.update();
    }
    void visit(sg::transform_t &transform) override {
        for (auto &child : transform.children()) {
            child->accept(*this);
        }
        transform.update();
    }
};
