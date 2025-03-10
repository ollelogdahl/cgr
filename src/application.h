#pragma once

#include "oc.h"

#include "rend2/render.h"
#include "rend2/render_state.h"
#include "rend2/render_storage.h"
#include "sg.h"
#include "gpu.h"
#include "render/imgui_renderer.h"

struct GLFWwindow;

struct ApplicationConfig {
    bool validation_layers = false;
};

class Application {
public:
    void init_and_run(const char *scene_file_path, const ApplicationConfig &config);

    GLFWwindow *window() const {
        return m_window;
    }

private:
    ref_t<sg::scene_t> m_current_scene;

    gpu_t m_gpu;

    RenderStorage *m_storage;
    RenderState *m_render_state;
    Renderer *m_renderer;
    ImGuiRenderer *m_gui_renderer;

    GLFWwindow *m_window;
};
