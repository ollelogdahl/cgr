#include "input_system.h"
#include "imgui/imgui.h"

InputSystem::InputSystem(GLFWwindow *window) : m_window(window) {

}

void InputSystem::disable_cursor() {
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}
void InputSystem::enable_cursor() {
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

v2f InputSystem::mouse_position() {
    f64 x, y;
    glfwGetCursorPos(m_window, &x, &y);
    return {(f32)x, (f32)y};
}

bool InputSystem::mouse_left_pressed() {
    auto &imgui_io = ImGui::GetIO();

    if (imgui_io.WantCaptureMouse) {
        return false;
    }

    return glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
}
bool InputSystem::key_pressed(i32 keycode) {
    auto &imgui_io = ImGui::GetIO();

    if (imgui_io.WantCaptureKeyboard) {
        return false;
    }

    return glfwGetKey(m_window, keycode) == GLFW_PRESS;
}
