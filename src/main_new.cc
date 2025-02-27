
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

std::string num_to_human(usize num);
std::string num_to_human_bytes(usize num);

void gui_metric();
void vma_query_metrics(gpu_t &gpu);

void add_many_objects(Renderer &renderer, MeshHandle mesh, usize count) {
    for (usize i = 0; i < count; ++i) {
        float x = sin(0.1 * i) * 8;
        float z = cos(0.1 * i) * 8;

        m4f rot = m4f::rotate(anglef::from_deg(((f32)i / count) * 360), {0, 1, 0});
        m4f position = m4f::translate({x, 0, z});

        auto obj = renderer.add_object();
        renderer.assign_geometry(obj, mesh);
        renderer.update_transform(obj, position * rot);
    }
}

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

    add_many_objects(m_renderer, mesh_handle, 100000);

    v3f camera_pos = {0, 2, 4};

    while (!glfwWindowShouldClose(m_window)) {
        gui.new_frame();

        // query vulkan vma for total memory usage
        vma_query_metrics(m_gpu);

        gui_metric();

        camera_pos.x = sin(glfwGetTime()) * 4;
        camera_pos.z = cos(glfwGetTime()) * 4;

        m_renderer.update_global(GlobalData{
            .view = m4f::look_at(camera_pos, {0, 0, 0}, {0, 1, 0}),
            .proj = m4f::perspective(anglef::from_deg(90.0), 1200.0f / 900.0f, 0.1f, 100.0f),
            .view_pos = camera_pos,
        });

        m_gpu.frame([&](gpu_t::frame_t &frame) {
            m_renderer.render(frame);
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

    for (u32 i = 0; i < VK_MAX_MEMORY_HEAPS; ++i) {
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
