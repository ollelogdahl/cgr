
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
