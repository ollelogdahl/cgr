#pragma once

#include "sg.h"

class RenderVisitor : public sg::node_visitor_t {
public:
    RenderVisitor(RenderState &render_state) : m_render_state(render_state) {
        transform_stack.push_back(m4f::identity());
        dirty_stack.push_back(false);
    }

    void visit(sg::geometry_t &geometry) override {
        if (dirty_stack.back()) {
            geometry.set_transform(transform_stack.back());
        }
    }

    void visit(sg::point_light_t &point_light) override {
        v4f p1 = v4f{point_light.position.x, point_light.position.y, point_light.position.z, 1};
        v3f position = (p1 * transform_stack.back()).xyz();

        m_lights.push_back(LightData{
            .position = p1,
            .color = {point_light.color.x, point_light.color.y, point_light.color.z, 1},
            .falloff_linear = point_light.linear,
            .falloff_quadratic = point_light.quadratic,
        });
    }

    void visit(sg::directional_light_t &directional_light) override {
        v4f d1 = v4f{directional_light.direction().x, directional_light.direction().y, directional_light.direction().z, 0};
        v3f direction = (d1 * transform_stack.back()).xyz();

        m_lights.push_back(LightData{
            .position = d1,
            .color = {directional_light.color().x, directional_light.color().y, directional_light.color().z, 1},
        });
    }

    void visit(sg::transform_t &transform) override {

        bool is_dirty = transform.is_dirty();
        m4f world_matrix = transform.get_local_matrix() * transform_stack.back();

        dirty_stack.push_back(is_dirty);
        transform_stack.push_back(world_matrix);
        transform.accept_children(*this);
        transform_stack.pop_back();
        dirty_stack.pop_back();
    }

    void visit(sg::camera_t &camera) override {
        // @todo: this could be a good place to transform the camera position into world space.
        // m_camera_position = camera.position();
        // m_renderer->set_view(camera.position(), camera.view_matrix());
        m_main_camera = &camera;
    }

    void reset_lights() {
        m_lights.clear();
    }

    std::span<const LightData> collected_lights() {
        return m_lights;
    }
    sg::camera_t &main_camera() {
        return *m_main_camera;
    }
private:
    RenderState &m_render_state;
    std::vector<m4f> transform_stack;
    std::vector<bool> dirty_stack;

    std::vector<LightData> m_lights;

    sg::camera_t *m_main_camera;
};
