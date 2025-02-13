#include <asm-generic/errno-base.h>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <deque>
#include <stdexcept>

#include <sys/types.h>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "oc.h"
#include "log.h"

#include <iostream>
#include <vector>
#include <cstring>
#include <set>
#include <vulkan/vulkan_core.h>


#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_vulkan.h>
#include <implot/implot.h>

#include "gpu.h"
#include "modimp.h"
#include "renderer.h"
#include "resource.h"
#include "camera.h"
#include "freefly_controller.h"
#include "vks.h"

#include "sg.h"

#include <time.h>

#include "sg/renderer_visitor.h"
#include "sg/bounds_visitor.h"

struct cpu_timer_t {
    cpu_timer_t() {
        memset(measures, 0, sizeof(measures));
        measure_idx = 0;
    }
    void start() {
        timespec time1;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time1);

        start_time = time1.tv_sec * 1e9 + time1.tv_nsec;
    }

    void stop() {
        timespec time2;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time2);

        // f32 time_ns = (time2.tv_sec * 1e9 + time2.tv_nsec - start_time);
        f32 time_ms = (time2.tv_sec * 1e3 + time2.tv_nsec / 1e6 - start_time / 1e6);

        measures[measure_idx] = time_ms;
        measure_idx = (measure_idx + 1) % array_size(measures);
    }

    f32 measure_ms() {
        i64 idx = (i64)measure_idx - 1;
        if (idx < 0) {
            idx = array_size(measures) - 1;
        }
        return measures[idx];
    }

    u32 size() {
        return array_size(measures);
    }

    u64 start_time = 0;
    f32 measures[256];
    u32 measure_idx = 0;
};

// @todo: aaaah correctness!!!
struct gpu_timer_t {
    void init(gpu_t &gpu) {
        VkQueryPoolCreateInfo query_pool_info = {};
        query_pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query_pool_info.queryCount = 2;
        VK_CHECK(vkCreateQueryPool(gpu.device, &query_pool_info, nullptr, &query_pool));
    }

    void reset(gpu_t &gpu, VkCommandBuffer &cmds) {
        if (!is_first) vkGetQueryPoolResults(
           	gpu.device,
           	query_pool,
           	0,
           	2,
           	4 * sizeof(u64),
           	timestamps,
           	2 * sizeof(u64),
           	VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        is_first = false;

        vkCmdResetQueryPool(cmds, query_pool, 0, 2);
    }

    void start(gpu_t &gpu, VkCommandBuffer &cmds) {
        if (timestamps[1] != 0 && timestamps[3] != 0) {
            auto as_nanos = (timestamps[2] - timestamps[0]) / gpu.limits.timestamp_period;
            measures[measure_idx] = (f32)as_nanos / 1e6;
            measure_idx = (measure_idx + 1) % array_size(measures);
        }

        vkCmdWriteTimestamp(cmds, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 0);
    }
    void stop(gpu_t &gpu, VkCommandBuffer &cmds) {
        vkCmdWriteTimestamp(cmds, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 1);

        (void)gpu;
    }

    u64 timestamps[4] = {0};
    f32 measures[256] = {0};
    u32 measure_idx = 0;
    bool is_first = true;
    VkQueryPool query_pool;
};

void gui();

class log_dump_visitor_t : public sg::node_visitor_t {
public:

    void print_bounds(sg::node_t &node) {
        g_log.info("{:{}s}aabb: {}", "", depth * 2 + 4, node.bounding_box());
    }

    void visit(sg::group_t &group) override {
        g_log.info("{:{}s}group {}", "", depth * 2, (void *)&group);
        print_bounds(group);

        depth++;
        group.accept_children(*this);
        depth--;
    }
    void visit(sg::geometry_t &geometry) override {
        g_log.info("{:{}s}geometry {}", "", depth * 2, (void *)&geometry);
        print_bounds(geometry);
    }
    void visit(sg::point_light_t &point_light) override {
        g_log.info("{:{}s}point_light {}", "", depth * 2, (void *)&point_light);
    }
    void visit(sg::transform_t &transform) override {
        g_log.info("{:{}s}transform {}", "", depth * 2, (void *)&transform);
        print_bounds(transform);

        depth++;
        transform.accept_children(*this);
        depth--;
    }
    void visit(sg::camera_t &camera) override {
        g_log.info("{:{}s}camera {}", "", depth * 2, (void *)&camera);
    }
    void visit(sg::lod_t &lod) override {
        g_log.info("{:{}s}lod {}", "", depth * 2, (void *)&lod);
        print_bounds(lod);

        depth++;
        lod.accept_children(*this);
        depth--;
    }
private:
    u32 depth = 0;
};

renderer_visitor_t g_renderer_visitor;
loader_t g_loader;

int main(int argc, char **argv) {

    oc_init();
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1200, 900, "vulkan", nullptr, nullptr);

    // initialize vulkan
    gpu_t gpu;
    gpu.init(window);
    g_log.info("gpu initialized");

    renderer_t renderer;

    g_loader.init(gpu);

    renderer.init(gpu, g_loader);

    if (argc < 2) {
        g_log.error("no scene file provided");
        return 1;
    }

    auto scene = g_loader.load_scene(argv[1]);

    g_renderer_visitor = renderer_visitor_t();

    compute_bounds_visitor_t bounds_visitor;
    scene->accept(bounds_visitor);

    cpu_timer_t full_loop_timer;

    auto camera = camera_t(
        v3f{0, 0, 5}, v3f{0, 0, 0},
        m4f::perspective(anglef::from_deg(80.0f), 1200.0f / 900.0f, 0.01f, 100.0f)
    );
    freefly_controller_t controller;
    controller.camera = &camera;

    g_renderer_visitor.renderer = &renderer;
    g_renderer_visitor.camera = &camera;

    float t = 0.0f;
    g_log.info("running...");
    while(!glfwWindowShouldClose(window)) {
        t += 0.017f;

        g_loader.process_hotreload();
        renderer.new_frame();

        full_loop_timer.start();

        controller.update(window, 0.16);

        gui();

        scene->accept(g_renderer_visitor);

        // @todo: move into scene graph
        renderer.set_camera(camera);

        v3f lamp1_pos = v3f{4 * sin(t), 2, 4 * cos(t)};
        renderer.add_point_light({
            .position = lamp1_pos,
            .color = v3f{1, 1, 1},
            .linear = 0.09f,
            .quadratic = 0.032f,
        });

        renderer.update_frame_data();
        gpu.frame([&](gpu_t::frame_t &frame) {
            renderer.draw(frame);
        });

        glfwPollEvents();

        full_loop_timer.stop();
    }

    glfwDestroyWindow(window);

    glfwTerminate();
}

void gui() {
    static bool show_imgui_demo = false;
    static bool show_implot_demo = false;
    if (show_imgui_demo)
        ImGui::ShowDemoWindow(&show_imgui_demo);

    if (show_implot_demo)
        ImPlot::ShowDemoWindow(&show_implot_demo);

    ImGui::Begin("test");

    ImGui::Checkbox("lod override", &g_renderer_visitor.lod_override);
    ImGui::SliderFloat("lod p", &g_renderer_visitor.lod_p, 0.0f, 100.0f);

    auto mods = g_loader.get_loaded_models();
    if (ImGui::BeginListBox("models")) {
        for (auto &model : mods) {
            for (auto &mesh : model.meshes) {
                for (auto &lod : mesh.lods) {
                    ImGui::Text("lod: %d", lod.index_count);
                }
            }
        }
        ImGui::EndListBox();
    }


    if (ImGui::Button("Show ImGui demo")) show_imgui_demo = !show_imgui_demo;
    if (ImGui::Button("Show ImPlot demo")) show_implot_demo = !show_implot_demo;
    ImGui::End();
}
