#include "freefly_controller.h"

#include "application.h"

v3f inline glfw_eqwasd(GLFWwindow *window);

void FreeflyController::update(sg::camera_t &camera, float dt) {
    auto move_relative = [&](v3f relative) {
        auto forward = camera.forward();
        auto right = v3f::cross(forward, camera.up());
        auto up = camera.up();

        camera.set_position(camera.position() + forward * relative.z + right * relative.x + up * relative.y);
    };

    auto rotate = [&](anglef pitch, anglef yaw) {
        auto forward = camera.forward();
        auto right = v3f::cross(forward, camera.up());
        auto up = camera.up();

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

        camera.set_forward(new_forward);
    };

    auto &imgui_io = ImGui::GetIO();

    if (!imgui_io.WantCaptureKeyboard) {
        v3f relative = glfw_eqwasd(m_app->window());
        move_relative(relative * m_speed * dt);
    }

    if (!imgui_io.WantCaptureMouse) {
        bool lmb_pressed = glfwGetMouseButton(m_app->window(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (lmb_pressed) {
            glfwSetInputMode(m_app->window(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            f64 new_mouse_x, new_mouse_y;
            glfwGetCursorPos(m_app->window(), &new_mouse_x, &new_mouse_y);
            if (m_pressed_last_frame) {
                f32 dx = (f32)(new_mouse_x - m_mouse_x) * m_sensitivity;
                f32 dy = (f32)(new_mouse_y - m_mouse_y) * m_sensitivity;

                rotate(anglef::from_deg(dy), anglef::from_deg(dx));
            } else {
                m_pressed_last_frame = true;
            }

            m_mouse_x = new_mouse_x;
            m_mouse_y = new_mouse_y;
        } else {
            glfwSetInputMode(m_app->window(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            m_pressed_last_frame = false;
        }
    }
}

v3f inline glfw_eqwasd(GLFWwindow *window) {
    // eq are up and down
    // wasd are left and right
    // ijkl are forward and backward

    v3f result = v3f{0, 0, 0};
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        result.y += 1;
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        result.y -= 1;
    }
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        result.z += 1;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        result.z -= 1;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        result.x -= 1;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        result.x += 1;
    }
    return result;
}
