#pragma once

#include "sg.h"

class RenderVisitor : public sg::node_visitor_t {
public:
    RenderVisitor(RenderState &render_state) : m_render_state(render_state) {
        transform_stack.push_back(m4f::identity());
    }

    void visit(sg::group_t &group) override {
        if (group.is_dirty()) {
            group.accept_children(*this);
            group.unmark_dirty();
        }
    }

    void visit(sg::geometry_t &geometry) override {
        geometry.set_transform(transform_stack.back());
    }

    void visit(sg::point_light_t &point_light) override {
        v4f p1 = v4f{0, 0, 0, 1};
        v3f position = (p1 * transform_stack.back()).xyz();

        point_light.set_position(position);
    }

    void visit(sg::directional_light_t &directional_light) override {
        // v4f d1 = v4f{directional_light.direction().x, directional_light.direction().y, directional_light.direction().z, 0};
        // v3f direction = (d1 * transform_stack.back()).xyz();
        // m_lights.push_back(LightData{
        //     .position = d1,
        //     .color = {directional_light.color().x, directional_light.color().y, directional_light.color().z, 1},
        // });
    }

    void visit(sg::transform_t &transform) override {
        if (transform.is_dirty()) {
            m4f world_matrix = transform.get_local_matrix() * transform_stack.back();

            transform_stack.push_back(world_matrix);
            transform.accept_children(*this);
            transform_stack.pop_back();

            transform.unmark_dirty();
        }
    }

    void visit(sg::camera_t &camera) override {
        // @todo: this could be a good place to transform the camera position into world space.
        // m_camera_position = camera.position();
        // m_renderer->set_view(camera.position(), camera.view_matrix());
        m_main_camera = &camera;
    }

    sg::camera_t &main_camera() {
        return *m_main_camera;
    }
private:
    RenderState &m_render_state;
    std::vector<m4f> transform_stack;

    sg::camera_t *m_main_camera;
};
