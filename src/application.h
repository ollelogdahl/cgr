#pragma once

#include "oc.h"

#include "sg.h"
#include "gpu.h"
#include "renderer.h"
#include "resource.h"
#include "render/imgui_renderer.h"

struct GLFWwindow;

class Application {
public:
    void init_and_run(const char *scene_file_path);

    GLFWwindow *window() const {
        return m_window;
    }

private:
    ref_t<sg::scene_t> m_current_scene;

    gpu_t m_gpu;
    renderer_t m_renderer;
    ImGuiRenderer m_gui_renderer;
    loader_t m_loader;

    GLFWwindow *m_window;
};
