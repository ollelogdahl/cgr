#pragma once

#include "oc.h"
#include "linalg.h"
#include "camera.h"

#include <GLFW/glfw3.h>
#include <imgui/imgui.h>

v3f inline gflw_eqwasd(GLFWwindow *window);

struct freefly_controller_t {
    camera_t *camera;
    bool pressed_last_frame = false;
    f64 mouse_x, mouse_y;

    f32 sensitivity = 0.1;
    f32 speed = 0.1;

    void update(GLFWwindow *window, float dt) {
        auto &imgui_io = ImGui::GetIO();

        if (!imgui_io.WantCaptureKeyboard) {
            v3f relative = gflw_eqwasd(window);
            camera->move_relative(relative * speed * dt);
        }

        if (!imgui_io.WantCaptureMouse) {
            bool lmb_pressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            if (lmb_pressed) {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

                f64 new_mouse_x, new_mouse_y;
                glfwGetCursorPos(window, &new_mouse_x, &new_mouse_y);
                if (pressed_last_frame) {
                    f32 dx = (f32)(new_mouse_x - mouse_x) * sensitivity;
                    f32 dy = (f32)(new_mouse_y - mouse_y) * sensitivity;

                    camera->rotate(anglef::from_deg(dy), anglef::from_deg(dx));
                } else {
                    pressed_last_frame = true;
                }

                mouse_x = new_mouse_x;
                mouse_y = new_mouse_y;
            } else {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                pressed_last_frame = false;
            }
        }
    }
};

v3f inline gflw_eqwasd(GLFWwindow *window) {
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
