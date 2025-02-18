#include "application.h"

#include "imgui/imgui.h"
#include "log.h"
#include "sg/render_visitor.h"
#include "sg/update_visitor.h"

#include "control/freefly_controller.h"

#include <tracy/Tracy.hpp>

#include <GLFW/glfw3.h>

std::string num_to_human(usize num);

void Application::init_and_run(const char *scene_file_path) {
    oc_init();

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
    m_window = glfwCreateWindow(1200, 900, "cgr", nullptr, nullptr);

    m_gpu.init(m_window);
    g_log.info("gpu initialized");

    m_loader.init(m_gpu);

    m_renderer.init(m_gpu, m_loader);
    m_gui_renderer.init(m_gpu);

    m_current_scene = m_loader.load_scene(scene_file_path);

    // this is kind of a hack.
    m_current_scene->reset_to_initial_state();

    // renderer visitor
    RenderVisitor render_visitor(m_renderer);

    update_visitor_t update_visitor;

    // camera movement visitor
    struct camera_movement_visitor_t : sg::node_visitor_t {
        camera_movement_visitor_t(Application &app) : controller(app) {}

        void visit(sg::camera_t &camera) override {
            if (camera.controlled()) {
                controller.update(camera, dt);
            }
        }

        FreeflyController controller;
        float dt;
    } camera_movement_visitor(*this);

    camera_movement_visitor.dt = 1.0f;

    g_log.info("running...");
    while (!glfwWindowShouldClose(m_window)) {
        m_renderer.new_frame();
        m_gui_renderer.new_frame();

        {
            ZoneScopedN("update");

            m_current_scene->accept(camera_movement_visitor);
            m_current_scene->accept(update_visitor);

            if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            }

            if (glfwGetKey(m_window, GLFW_KEY_R) == GLFW_PRESS) {
                m_current_scene->reset_to_initial_state();
            }

            // @todo: configurable
            m_renderer.set_projection(
                m4f::perspective(anglef::from_deg(80.0f), 1200.0f / 900.0f, 0.01f, 100.0f)
            );

            m_current_scene->accept(render_visitor);

            m_renderer.prepare_drawing();
        }

        m_loader.process_hotreload();

        ImGui::Begin("Loaded Models");

        for (auto &[params, model] : m_loader.get_loaded_models()) {
            ImGui::BeginGroup();

            ImGui::Text("%s", params.path.c_str());
            ImGui::Text("  meshes: %lu", model.meshes.size());

            ImGui::Text("  LODs");
            for (u32 i = 0; i < model.meshes[0].lods.size(); ++i) {
                auto sum = 0;
                for (auto &m : model.meshes) {
                    sum += m.lods[i].index_count;
                }
                ImGui::Text("    %u: %s", i, num_to_human(sum).c_str());
            }

            ImGui::EndGroup();
        }

        ImGui::End();

        m_gpu.frame([&](gpu_t::frame_t &frame) {
            ZoneScopedN("frame-submit");

            m_renderer.draw(frame);
            m_gui_renderer.draw(frame);
        });

        {
            ZoneScopedN("poll-events");
            glfwPollEvents();
        }

        FrameMark;
    }
}

std::string num_to_human(usize num) {
    if (num < 1000) {
        return std::to_string(num);
    }
    else if (num < 1000000) {
        return fmt::format("{:.2f}k", (f32)num / 1000);
    }
    else {
        return fmt::format("{:.2f}M", (f32)num / 1000000);
    }
}
