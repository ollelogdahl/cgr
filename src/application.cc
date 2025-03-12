#include "application.h"

#include "imgui/imgui.h"
#include "log.h"
#include "metrics.h"
#include "rend2/render_storage.h"
#include "rend2/shader_compiler.h"

#include "control/freefly_controller.h"
#include "sg.h"
#include "sg/render_visitor.h"
#include "sg/update_visitor.h"

#include <tracy/Tracy.hpp>

#include <GLFW/glfw3.h>

std::string num_to_human(usize num);
std::string num_to_human_bytes(usize num);

void gui_metric();
void vma_query_metrics(gpu_t &gpu);

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

// @todo: break out!
class TracyProfilerRunner {
public:
    TracyProfilerRunner() {}
    void update(InputSystem &input) {

        if (input.key_pressed(GLFW_KEY_GRAVE_ACCENT)) {
            if (!m_pressed_last_frame && !m_tracy_running) {
                launch();
            }

            m_pressed_last_frame = true;
        } else {
            m_pressed_last_frame = false;
        }
    }
private:
    void launch() {
        m_tracy_pid = fork();
        if (m_tracy_pid == 0) {
            execlp("tracy-profiler", "tracy-profiler", "-a", "localhost", nullptr);
            exit(1);
        }
        m_tracy_running = true;
    }

    pid_t m_tracy_pid = -1;
    bool m_tracy_running = false;

    bool m_pressed_last_frame = false;
};

void Application::init_and_run(const char *scene_file_path, const ApplicationConfig &config) {
    oc_init();

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_FALSE);
    m_window = glfwCreateWindow(1600, 1200, "cgr", nullptr, nullptr);

    InputSystem input(m_window);

    m_gpu.init(m_window, {
        .request_validation_layers = config.validation_layers,
    });
    g_log.info("gpu initialized");

    const char *glslc_path = "glslc";
    if (const char *env = getenv("GLSLC_PATH")) {
        glslc_path = env;
    }

    ShaderCompiler shader_compiler(m_gpu, glslc_path);

    m_storage = new RenderStorage(m_gpu, {
        .max_objects = 200 * 1024,
        .max_vertices = 1 * 1024 * 1024,
        .max_indices = 1 * 1024 * 1024,
        .max_meshes = 256,
        .max_materials = 200 * 1024,
        .max_textures = 1024,
        .max_lights = 1024,
    });

    auto l1 = m_storage->alloc_light({
        .position = {0, 1, 0, 1},
        .color = {1, 1, 1, 1},
        .falloff_linear = 0.14f,
        .falloff_quadratic = 0.07f,
    });

    m_renderer = new Renderer(m_gpu, *m_storage, shader_compiler);
    m_render_state = new RenderState(m_gpu, *m_storage, *m_renderer, shader_compiler);

    m_gui_renderer = new ImGuiRenderer(m_gpu);

    // m_current_scene = m_loader.
    m_current_scene = make_ref<sg::scene_t>(*m_render_state);
    sg::load(*m_render_state, scene_file_path, *m_current_scene);



    // this is kind of a hack.
    m_current_scene->reset_to_initial_state();

    // renderer visitor
    RenderVisitor render_visitor(*m_render_state);

    update_visitor_t update_visitor;

    FreeflyController<sg::camera_t> camera_controller(input);

    TracyProfilerRunner tracy_runner;

    // fixed perspective projection
    f32 znear = 0.1f;
    f32 zfar = 100.0f;
    m4f persp = m4f::perspective(anglef::from_deg(90.0), 1200.0f / 900.0f, znear, zfar);

    g_log.info("running...");
    while (!glfwWindowShouldClose(m_window)) {
        m_gui_renderer->new_frame();

        vma_query_metrics(m_gpu);
        gui_metric();

        ImGui::Begin("Debug");

        const char *debug_modes[] = {
            "none",
            "unlit",
            "cluster_id",
            "cluster_lights",
        };
        static int debug_mode = 0;
        ImGui::Combo("Debug mode", &debug_mode, debug_modes, IM_ARRAYSIZE(debug_modes));
        ImGui::End();

        m_renderer->forward_pass().set_debug_mode((DebugMode)debug_mode);

        tracy_runner.update(input);

        sg::camera_t *main_camera;
        {
            ZoneScopedN("update");

            // m_current_scene->accept(camera_movement_visitor);
            m_current_scene->accept(update_visitor);

            {
                ZoneScopedN("render-visitor");
                render_visitor.reset_lights();
                m_current_scene->accept(render_visitor);
                main_camera = &render_visitor.main_camera();
            }
            assert(main_camera);

            if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            }

            if (glfwGetKey(m_window, GLFW_KEY_R) == GLFW_PRESS) {
                m_current_scene->reset_to_initial_state();
            }

            // @todo: how should we do delta-time?
            camera_controller.update(*main_camera, 1.0f / 15.0f);
        }

        View view = {
            .projection = persp,
            .view = m4f::look_at(main_camera->position(), main_camera->position() + main_camera->forward(), main_camera->up()),
            .position = main_camera->position(),
            .znear = znear,
            .zfar = zfar,
        };

        m_gpu.frame([&](gpu_t::frame_t &frame) {
            m_renderer->render(frame, view);
            m_gui_renderer->draw(frame);
        });

        {
            ZoneScopedN("poll-events");
            glfwPollEvents();
        }

        FrameMark;
    }
}

// @todo: break this out!
void gui_metric_metric(metrics::Metric &metric);
void gui_metric_node(const metrics::TreeNode &node);

void gui_metric() {
    ImGui::Begin("Metrics");

    auto &tree = metrics::get_metric_tree();
    for (auto &node : tree.children) {
        gui_metric_node(node);
    }
    for (auto &m : tree.metrics) {
        gui_metric_metric(*m);
    }

    ImGui::End();
}

void gui_metric_node(const metrics::TreeNode &node) {
    if (ImGui::TreeNodeEx(node.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        for (auto &child : node.children) {
            gui_metric_node(child);
        }
        for (auto &m : node.metrics) {
            gui_metric_metric(*m);
        }

        ImGui::TreePop();
    }
}
void gui_metric_metric(metrics::Metric &m) {
    std::visit(overloaded{
        [&](const u64 &v) {
            std::string s;
            if (m.unit[0] == 'b') {
                auto s = num_to_human_bytes(v);
                ImGui::Text("%s: %s", m.name, s.c_str());
            } else {
                auto s = num_to_human(v);
                ImGui::Text("%s: %s", m.name, s.c_str());
            }
        },
        [&](const u32 &v) {
            if (m.unit[0] == 'b') {
                auto s = num_to_human_bytes(v);
                ImGui::Text("%s: %s", m.name, s.c_str());
            } else {
                auto s = num_to_human(v);
                ImGui::Text("%s: %s", m.name, s.c_str());
            }
        },
        [&](const f32 &v) {
            if (m.unit[0] == 'p') {
                ImGui::Text("%s: %.2f%%", m.name, v * 100);
            } else {
                ImGui::Text("%s: %f", m.name, v);
            }
        },
    }, m.value);
}

std::string num_to_human(usize num) {
    if (num < 1000) {
        return std::to_string(num);
    }
    else if (num < 1000 * 1000) {
        return fmt::format("{:.2f}k", (f32)num / 1000);
    }
    else if (num < 1000 * 1000 * 1000) {
        return fmt::format("{:.2f}m", (f32)num / (1000 * 1000));
    }
    else {
        return fmt::format("{:.2f}b", (f32)num / (1000 * 1000 * 1000));
    }
}

std::string num_to_human_bytes(usize num) {
    if (num < 1024) {
        return std::to_string(num);
    }
    else if (num < 1024 * 1024) {
        return fmt::format("{:.2f}Ki", (f32)num / 1024);
    }
    else if (num < 1024 * 1024 * 1024) {
        return fmt::format("{:.2f}Mi", (f32)num / (1024 * 1024));
    }
    else {
        return fmt::format("{:.2f}Gi", (f32)num / (1024 * 1024 * 1024));
    }
}

void vma_query_metrics(gpu_t &gpu) {
    // @todo: funk! It seems like i have a heap on my gpu that is
    // waay larger than my vram and is messing up the metrics.
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
    vmaGetHeapBudgets(gpu.allocator, budgets);

    u64 sum_alloc_count = 0;
    u64 sum_alloc_bytes = 0;
    u64 sum_block_bytes = 0;
    u64 sum_usage = 0;
    u64 sum_budget = 0;

    for (u32 i = 0; i < 1; ++i) {
        sum_alloc_count += budgets[i].statistics.allocationCount;
        sum_alloc_bytes += budgets[i].statistics.allocationBytes;
        sum_block_bytes += budgets[i].statistics.blockBytes;
        sum_usage += budgets[i].usage;
        sum_budget += budgets[i].budget;
    }

    metrics::gauge_u64("vma.alloc_count", sum_alloc_count);
    metrics::gauge_u64("vma.alloc_bytes", sum_alloc_bytes, "b");
    metrics::gauge_u64("vma.block_bytes", sum_block_bytes, "b");
    metrics::gauge_u64("vma.usage", sum_usage, "b");
    metrics::gauge_u64("vma.budget", sum_budget, "b");
}
