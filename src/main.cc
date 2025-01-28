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

#include <time.h>

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

loader_t g_loader;
renderer_t g_renderer;

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
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

    g_loader.init(gpu);

    g_renderer.init(gpu, g_loader);

    const char *model_path = "assets/dragon.obj";
    if (argc > 1) {
        model_path = argv[1];
    }

    auto mesh = g_loader.load_model({
        .path = model_path,
        .lod_settings = {
            { 10.0f, 4e-3f },
            { 20.0f, 1e-2f },
        }
    });

    auto tex_color = g_loader.load_texture({.path = "assets/img0.jpg"});
    auto tex_normal = g_loader.load_texture({.path = "assets/tiles074_normal.jpg"});
    auto tex_roughness = g_loader.load_texture({.path = "assets/tiles074_roughness.jpg"});

    cpu_timer_t full_loop_timer;

    camera_t camera = camera_t(
        v3f{0, 0, 5}, v3f{0, 0, 0},
        m4f::perspective(anglef::from_deg(80.0f), 1200.0f / 900.0f, 0.01f, 20.0f)
    );
    freefly_controller_t controller;
    controller.camera = &camera;

    v3f scale = {0.02, 0.02, 0.02};
    bool lod_override = false;
    i32 lod_override_value = 0;

    draw_element_material_t material = {
        .flags = draw_element_flags_t::use_albedo_tex,
        .color = v3f{0.3, 0.3, 0.3},
        .roughness = 0.5f,
        .metallic = 0.5f,
        .albedo_tex_idx = g_renderer.define_texture(tex_color),
        .normal_tex_idx = g_renderer.define_texture(tex_normal),
        .roughness_tex_idx = g_renderer.define_texture(tex_roughness),
    };

    g_log.info("running...");
    while(!glfwWindowShouldClose(window)) {
        g_loader.process_hotreload();
        g_renderer.new_frame();

        full_loop_timer.start();

        controller.update(window, 0.16);

        static bool show_imgui_demo = false;
        static bool show_implot_demo = false;
        if (show_imgui_demo)
            ImGui::ShowDemoWindow(&show_imgui_demo);

        if (show_implot_demo)
            ImPlot::ShowDemoWindow(&show_implot_demo);

        ImGui::Begin("test");

        {
            if (ImPlot::BeginPlot("Frame Times")) {
                ImPlot::SetupAxes("Frame", "Time (ms)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
                ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);
                ImPlot::PlotLine("Loop", full_loop_timer.measures, array_size(full_loop_timer.measures));
                ImPlot::EndPlot();
            }
            auto last_frame_time = full_loop_timer.measure_ms();
            ImGui::Text("Frame time: %.2f ms", last_frame_time);
            ImGui::Text("FPS: %.2f", 1000.0f / last_frame_time);
        }

        {
            ImGui::SeparatorText("Debug");
            ImGui::Checkbox("LOD override", &lod_override);
            ImGui::SliderInt("LOD", &lod_override_value, 0, mesh->meshes[0].lods.size() - 1);
        }

        {
            ImGui::SeparatorText("Material");
            ImGui::ColorEdit3("Color", &material.color.x);
            ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f);
            ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f);

            ImGui::CheckboxFlags("Use Albedo Texture", (int *)&material.flags, (int)draw_element_flags_t::use_albedo_tex);
            ImGui::CheckboxFlags("Use Normal Texture", (int *)&material.flags, (int)draw_element_flags_t::use_normal_tex);
            ImGui::CheckboxFlags("Use Roughness Texture", (int *)&material.flags, (int)draw_element_flags_t::use_roughness_tex);
        }

        {
            ImGui::SeparatorText("Transform");
            ImGui::SliderFloat3("Scale", &scale.x, 0.0f, 1.0f);
        }

        if (ImGui::Button("Show ImGui demo")) show_imgui_demo = !show_imgui_demo;
        if (ImGui::Button("Show ImPlot demo")) show_implot_demo = !show_implot_demo;
        ImGui::End();

        g_renderer.add_model({
            .model = mesh.get(),
            .transform = m4f::translate(v3f{0, 0, 0}) * m4f::scale(scale),
            .material = material,
            .lod = 0,
        });

        g_renderer.set_camera(camera);

        g_renderer.update_frame_data();
        gpu.frame([&](gpu_t::frame_t &frame) {
            g_renderer.draw(frame);
        });

        glfwPollEvents();

        full_loop_timer.stop();
    }

    glfwDestroyWindow(window);

    glfwTerminate();
}
