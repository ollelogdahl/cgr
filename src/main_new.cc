
#include "gpu.h"
#include "log.h"
#include "oc.h"
#include "rend2/render.h"
#include <GLFW/glfw3.h>

#include <tracy/Tracy.hpp>

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
    auto mesh_handle = m_renderer.add_mesh(m);

    auto obj0 = m_renderer.add_object();
    m_renderer.assign_geometry(obj0, mesh_handle);

    while (!glfwWindowShouldClose(m_window)) {
        m_gpu.frame([&](gpu_t::frame_t &frame) {
            ZoneScopedN("frame-submit");
            m_renderer.render(frame);
        });

        {
            ZoneScopedN("poll-events");
            glfwPollEvents();
        }
    }

    return 0;
}
