
#include "gpu.h"
#include "imgui/imgui.h"
#include "log.h"
#include "metrics.h"
#include "oc.h"
#include "rend2/render.h"
#include <GLFW/glfw3.h>

#include <tracy/Tracy.hpp>
#include <variant>

#include "render/imgui_renderer.h"

Mesh simple_mesh();

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

int main(void) {
    oc_init();

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_FALSE);
    auto m_window = glfwCreateWindow(1200, 900, "cgr", nullptr, nullptr);

    gpu_t m_gpu;
    m_gpu.init(m_window, {
        .request_validation_layers = true,
    });
    g_log.info("gpu initialized");

    Renderer m_renderer(m_gpu);
    ImGuiRenderer gui(m_gpu);

    Mesh m = simple_mesh();
    auto mesh_handle = m_renderer.add_mesh(m);

    for (auto i = 0; i < 1000; ++i) {
        auto obj = m_renderer.add_object();
        m_renderer.assign_geometry(obj, mesh_handle);
        m_renderer.update_transform(obj, m4f::translate({0, 0, -(f32)i}));
    }

    m_renderer.update_global(GlobalData{
        .view = m4f::look_at({0, 0, 2}, {0, 0, 0}, {0, 1, 0}),
        .proj = m4f::perspective(anglef::from_deg(60.0), 1200.0f / 900.0f, 0.1f, 100.0f),
        .view_pos = {0, 0, 2},
    });

    while (!glfwWindowShouldClose(m_window)) {
        gui.new_frame();

        {
            ZoneScopedN("metrics-fetch");
            ImGui::Begin("Metrics");
            auto metrics = metrics::get_metrics();

            for (auto &m : metrics) {
                std::visit(overloaded{
                    [&](const u64 &v) {
                        ImGui::Text("%s: %lu", m.name, v);
                    },
                    [&](const u32 &v) {
                        ImGui::Text("%s: %u", m.name, v);
                    },
                    [&](const f32 &v) {
                        ImGui::Text("%s: %f", m.name, v);
                    },
                }, m.value);
            }

            ImGui::End();
        }

        m_gpu.frame([&](gpu_t::frame_t &frame) {
            m_renderer.render(frame);
            gui.draw(frame);
        });

        {
            ZoneScopedN("poll-events");
            glfwPollEvents();
        }
    }

    return 0;
}

Mesh simple_mesh() {
    Mesh m = {};
    auto &lod0 = m.lods.emplace_back();
    lod0.indices = {0, 1, 2};
    m.vertices = {
        {0, 0, 0},
        {1, 0, 0},
        {0, 1, 0},
    };
    m.uvs = {
        {0, 0},
        {1, 0},
        {0, 1},
    };
    m.normals = {
        {0, 0, 1},
        {0, 0, 1},
        {0, 0, 1},
    };
    m.colors = {
        0xff0000ff,
        0x00ff00ff,
        0x0000ffff,
    };

    return m;
}
