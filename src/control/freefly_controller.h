#pragma once

#include "oc.h"
#include "linalg.h"

#include "controllable.h"
#include "input_system.h"

template <Controllable T>
class FreeflyController {
public:
    FreeflyController(InputSystem &input) : m_input(&input) {}
    void update(T &object, float dt) {
        auto move_relative = [&](v3f relative) {
            auto forward = object.forward();
            auto right = v3f::cross(forward, object.up());
            auto up = object.up();

            object.set_position(object.position() + forward * relative.z + right * relative.x + up * relative.y);
        };

        auto rotate = [&](anglef pitch, anglef yaw) {
            auto forward = object.forward();
            auto right = v3f::cross(forward, object.up());
            auto up = object.up();

            // limit rotation to not be directly up or down
            auto dot = v3f::dot(up, forward);
            if (dot > 0.98) {
                pitch = std::max(pitch, anglef::zero());
            }
            if (dot < -0.98) {
                pitch = std::min(pitch, anglef::zero());
            }

            auto rotation = m4f::rotate(pitch, right) * m4f::rotate(yaw, up);
            auto new_forward = (v4f{forward.x, forward.y, forward.z, 0} * rotation).xyz();

            object.set_forward(new_forward);
        };

        v3f relative = eqwasd();
        move_relative(relative * dt);

        if (m_input->mouse_left_pressed()) {
            m_input->disable_cursor();

            v2f new_mouse = m_input->mouse_position();
            if (m_pressed_last_frame) {
                f32 dx = (new_mouse.x - m_mouse_x) * m_sensitivity;
                f32 dy = (new_mouse.y - m_mouse_y) * m_sensitivity;

                rotate(anglef::from_deg(dy), anglef::from_deg(dx));
            } else {
                m_pressed_last_frame = true;
            }

            m_mouse_x = new_mouse.x;
            m_mouse_y = new_mouse.y;
        } else {
            m_input->enable_cursor();
            m_pressed_last_frame = false;
        }
    }
private:
    v3f eqwasd() {
        v3f relative = {0, 0, 0};
        if (m_input->key_pressed(GLFW_KEY_W)) {
            relative.z += 1;
        }
        if (m_input->key_pressed(GLFW_KEY_S)) {
            relative.z -= 1;
        }
        if (m_input->key_pressed(GLFW_KEY_A)) {
            relative.x -= 1;
        }
        if (m_input->key_pressed(GLFW_KEY_D)) {
            relative.x += 1;
        }
        if (m_input->key_pressed(GLFW_KEY_E)) {
            relative.y += 1;
        }
        if (m_input->key_pressed(GLFW_KEY_Q)) {
            relative.y -= 1;
        }
        return relative;
    }

    InputSystem *m_input;

    bool m_pressed_last_frame = false;
    f64 m_mouse_x, m_mouse_y;

    f32 m_sensitivity = 0.1;
    f32 m_speed = 0.1;
};
