#pragma once

#include "linalg.h"
#include <GLFW/glfw3.h>

class InputSystem {
public:
    InputSystem(GLFWwindow *window);

    void disable_cursor();
    void enable_cursor();

    v2f mouse_position();

    bool mouse_left_pressed();
    bool key_pressed(i32 keycode);
private:
    GLFWwindow *m_window;
};
