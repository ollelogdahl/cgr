
#include "control/freefly_controller.h"
#include "control/input_system.h"
#include "gpu.h"
#include "imgui/imgui.h"
#include "log.h"
#include "metrics.h"
#include "oc.h"
#include "rend2/render.h"
#include <GLFW/glfw3.h>

#include "model.h"

#include <tracy/Tracy.hpp>
#include <variant>

#include "render/imgui_renderer.h"

Mesh simple_mesh();

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

std::string num_to_human(usize num);
std::string num_to_human_bytes(usize num);

void gui_metric();
void vma_query_metrics(gpu_t &gpu);

v4f color_from_hsv(f32 h, f32 s, f32 v) {
    f32 c = v * s;
    f32 x = c * (1 - std::abs(fmod(h / 60.0, 2) - 1));
    f32 m = v - c;

    f32 r, g, b;
    if (h < 60) {
        r = c;
        g = x;
        b = 0;
    } else if (h < 120) {
        r = x;
        g = c;
        b = 0;
    } else if (h < 180) {
        r = 0;
        g = c;
        b = x;
    } else if (h < 240) {
        r = 0;
        g = x;
        b = c;
    } else if (h < 300) {
        r = x;
        g = 0;
        b = c;
    } else {
        r = c;
        g = 0;
        b = x;
    }

    return {r + m, g + m, b + m, 1};
}

void dragons_in_grid(Renderer &renderer, f32 spacing, usize size) {
    std::vector<LODSetting> lods = {
        LODSetting{.min_distance = 10, .target_error = 1e-3},
        LODSetting{.min_distance = 20, .target_error = 4e-3},
        LODSetting{.min_distance = 40, .target_error = 2e-2},
    };
    Model mod = load_model("assets/dragon.obj", lods);
    Mesh m = mod.meshes[0];

    ShaderHandle shader = renderer.load_shader({
        .glsl_vert_path = "shaders/forward.vert",
        .glsl_frag_path = "shaders/forward.frag",
    });

    MeshHandle mesh_handle = renderer.add_mesh(m);

    for (usize i = 0; i < size; ++i) {
        f32 halfx = (f32)(size - 1) * spacing / 2;
        for (usize j = 0; j < size; ++j) {
            f32 halfz = (f32)(size - 1) * spacing / 2;
            for (usize k = 0; k < size; ++k) {
                f32 halfy = (f32)(size - 1) * spacing / 2;

                v3f translate = {(f32)i * spacing - halfx, (f32)k * spacing - halfy, (f32)j * spacing - halfz};
                // translate.y += 300.0f;
                m4f scale = m4f::scale({0.02, 0.02, 0.02});
                m4f transform = scale * m4f::translate(translate);

                f32 random_hue = (f32)rand() / (f32)RAND_MAX * 360;
                v4f color = color_from_hsv(random_hue, 0.8, 0.8);

                auto obj = renderer.add_object();

                auto mat = renderer.add_material(MaterialData{
                    .color = color,
                });

                renderer.assign_geometry(obj, mesh_handle);
                renderer.assign_material(obj, mat);
                renderer.assign_shader(obj, shader);
                renderer.update_transform(obj, transform);
            }
        }
    }
}

int main(int argc, char **argv) {
    oc_init();

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_FALSE);
    auto m_window = glfwCreateWindow(1200, 900, "cgr", nullptr, nullptr);

    InputSystem input(m_window);

    bool validation_layers = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--validate") == 0) {
            validation_layers = true;
        }
    }

    gpu_t m_gpu;
    m_gpu.init(m_window, {
        .request_validation_layers = validation_layers,
    });
    g_log.info("gpu initialized");

    const char *glslc_path = "glslc";
    if (const char *env = getenv("GLSLC_PATH")) {
        glslc_path = env;
    }

    ShaderCompiler shader_compiler(m_gpu, glslc_path);

    Renderer m_renderer(m_gpu, shader_compiler);
    ImGuiRenderer gui(m_gpu);

    dragons_in_grid(m_renderer, 5.0f, 50);

    class Camera {
    public:
        v3f position() const { return m_position; }
        v3f forward() const { return m_forward; }
        v3f up() const { return {0, 1, 0}; }

        void set_position(v3f pos) { m_position = pos; }
        void set_forward(v3f forward) { m_forward = v3f::normalize(forward); }
    private:
        v3f m_position;
        v3f m_forward;
    };
    Camera camera;
    camera.set_position({0, 0, 0});
    camera.set_forward({0, 0, 1});

    FreeflyController camera_controller(camera, input);

    m4f persp = m4f::perspective(anglef::from_deg(90.0), 1200.0f / 900.0f, 0.1f, 400.0f);

    while (!glfwWindowShouldClose(m_window)) {
        gui.new_frame();

        // query vulkan vma for total memory usage
        vma_query_metrics(m_gpu);

        gui_metric();

        camera_controller.update(1.0f / 30.0f);

        m_renderer.update_global(GlobalData{
            .view = m4f::look_at(camera.position(), camera.position() + camera.forward(), camera.up()),
            .proj = persp,
            .view_pos = camera.position(),
        });
        View view = {
            .projection = persp,
            .view = m4f::look_at(camera.position(), camera.position() + camera.forward(), camera.up()),
            .position = camera.position(),
        };

        m_gpu.frame([&](gpu_t::frame_t &frame) {
            m_renderer.render(frame, view);
            gui.draw(frame);
        });

        {
            ZoneScopedN("poll-events");
            glfwPollEvents();
        }

        FrameMark;
    }

    return 0;
}

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

Mesh simple_mesh() {
    Mesh m = {};
    auto &lod0 = m.lods.emplace_back();
    lod0.indices = {0, 1, 2};
    m.vertices = {
        {-1, 0, 0},
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
    m.bounds = aabb_t{
        {-1, 0, 0},
        {1, 1, 0},
    };

    return m;
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
