#pragma once

#include "renderer.h"
#include "sg.h"

class RenderVisitor : public sg::node_visitor_t {
public:
    RenderVisitor(renderer_t &renderer) {
        m_renderer = &renderer;
        transform_stack.push_back(m4f::identity());
    }

    void visit(sg::geometry_t &geometry) override {
        // @todo: extract from state.
        m_renderer->add_draw_indexed({
            .vertex_buffer = geometry.vertex_buffer.get(),
            .index_buffer = geometry.index_buffer.get(),
            .index_count = geometry.index_count,
            .vertex_offset = 0,
            .index_offset = 0,
            .transform = transform_stack.back(),
            .material = geometry.state().material.get(),
        });
    }

    void visit(sg::point_light_t &point_light) override {
        v4f p1 = v4f{point_light.position.x, point_light.position.y, point_light.position.z, 1};
        v3f position = (p1 * transform_stack.back()).xyz();

        m_renderer->add_point_light({
            .position = position,
            .color = point_light.color,
            .linear = point_light.linear,
            .quadratic = point_light.quadratic,
        });
    }

    void visit(sg::directional_light_t &directional_light) override {
        v4f d1 = v4f{directional_light.direction().x, directional_light.direction().y, directional_light.direction().z, 0};
        v3f direction = (d1 * transform_stack.back()).xyz();

        m_renderer->add_directional_light({
            .direction = direction,
            .color = directional_light.color(),
        });
    }

    void visit(sg::transform_t &transform) override {
        transform_stack.push_back(transform.get_local_matrix() * transform_stack.back());
        transform.accept_children(*this);
        transform_stack.pop_back();
    }

    void visit(sg::camera_t &camera) override {
        // @todo: this could be a good place to transform the camera position into world space.
        m_camera_position = camera.position();
        m_renderer->set_view(camera.position(), camera.view_matrix());
    }

    void visit(sg::lod_t &lod) override {
        // we have the camera in world space, but we need it in object space.
        v3f position_ws = (v4f{0, 0, 0, 1} * transform_stack.back()).xyz();
        lod.set_center(m_camera_position - position_ws);

        lod.traverse(*this);
    }
private:
    renderer_t *m_renderer = nullptr;
    std::vector<m4f> transform_stack;
    v3f m_camera_position;
};
