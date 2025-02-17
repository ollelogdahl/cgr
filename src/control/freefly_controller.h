#pragma once

#include "oc.h"
#include "linalg.h"

#include <GLFW/glfw3.h>
#include <imgui/imgui.h>

#include "sg.h"

class Application;

class FreeflyController {
public:
    FreeflyController(Application &app) : m_app(&app) {}
    void update(sg::camera_t &camera, float dt);
private:
    Application *m_app;

    bool m_pressed_last_frame = false;
    f64 m_mouse_x, m_mouse_y;

    f32 m_sensitivity = 0.1;
    f32 m_speed = 0.1;
};
