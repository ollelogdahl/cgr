#pragma once

#include "camera.h"
#include "renderer.h"
#include "sg.h"

class renderer_visitor_t : public sg::node_visitor_t {
public:
    bool lod_override = false;
    f32 lod_p = 0.0f;
    // @todo: real camera
    camera_t *camera = nullptr;
    renderer_t *renderer = nullptr;

    renderer_visitor_t() {
        transform_stack.push_back(m4f::identity());
    }

    void visit(sg::group_t &group) override {
        group.accept_children(*this);
    }

    void visit(sg::geometry_t &geometry) override {
        // @todo: extract from state.
        renderer->add_draw_indexed({
            .vertex_buffer = geometry.vertex_buffer,
            .index_buffer = geometry.index_buffer,
            .index_count = geometry.index_count,
            .vertex_offset = 0,
            .index_offset = 0,
            .transform = transform_stack.back(),
            .material = geometry.state().material,
        });
    }

    void visit(sg::point_light_t &point_light) override {

        v4f p1 = v4f{point_light.position.x, point_light.position.y, point_light.position.z, 1};
        v3f position = (p1 * transform_stack.back()).xyz();

        renderer->add_point_light({
            .position = position,
            .color = point_light.color,
            .linear = point_light.linear,
            .quadratic = point_light.quadratic,
        });
    }
    void visit(sg::transform_t &transform) override {
        transform_stack.push_back(transform.get_local_matrix() * transform_stack.back());
        transform.accept_children(*this);
        transform_stack.pop_back();
    }
    void visit(sg::camera_t &camera) override {
        (void)camera;
    }
    void visit(sg::lod_t &lod) override {
        if (lod_override) {
            lod.set_center(v3f{0, lod_p, 0});
        } else {
            // we have the camera in world space, but we need it in object space.
            v3f position_ws = (v4f{0, 0, 0, 1} * transform_stack.back()).xyz();
            lod.set_center(camera->position - position_ws);
        }

        lod.traverse(*this);

    }
private:
    std::vector<m4f> transform_stack;
};
