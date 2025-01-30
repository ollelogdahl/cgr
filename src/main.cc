#include <asm-generic/errno-base.h>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <deque>
#include <stdexcept>

#include <sys/types.h>
#include <tinyxml2.h>
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
void load_scene(const char *path);

loader_t g_loader;
renderer_t g_renderer;
sg::scene_t g_scene;

class log_dump_visitor_t : public sg::node_visitor_t {
public:
    void visit(sg::group_t &group) override {
        g_log.info("{:{}s}group {}", "", depth * 2, (void *)&group);
        depth++;
        group.accept_children(*this);
        depth--;
    }
    void visit(sg::geometry_t &geometry) override {
        g_log.info("{:{}s}geometry {}", "", depth * 2, (void *)&geometry);
        (void)geometry;
    }
    void visit(sg::point_light_t &point_light) override {
        g_log.info("{:{}s}point_light {}", "", depth * 2, (void *)&point_light);
        (void)point_light;
    }
    void visit(sg::transform_t &transform) override {
        g_log.info("{:{}s}transform {}", "", depth * 2, (void *)&transform);
        depth++;
        transform.accept_children(*this);
        depth--;
    }
private:
    u32 depth = 0;
};

class renderer_visitor_t : public sg::node_visitor_t {
public:
    renderer_visitor_t(renderer_t *renderer) : renderer(renderer) {
        transform_stack.push_back(m4f::identity());
    }

    void visit(sg::group_t &group) override {
        (void)group;
    }
    void visit(sg::geometry_t &geometry) override {
        // @todo: extract from state.
        draw_element_material_t material = {
            .flags = (draw_element_flags_t)0,
            .color = v3f{0.3, 0.3, 0.3},
            .roughness = 0.5f,
            .metallic = 0.5f,
            .albedo0_idx = 0,
            .albedo1_idx = 0,
            .albedo2_idx = 0,
            .normal_idx = 0,
            .roughness_idx = 0,
        };

        renderer->add_draw_indexed({
            .vertex_buffer = geometry.mesh->vertex_buffer,
            .index_buffer = geometry.mesh->lods[0].index_buffer,
            .index_count = geometry.mesh->lods[0].index_count,
            .vertex_offset = 0,
            .index_offset = 0,
            .transform = transform_stack.back(),
            .material = material
        });
    }
    void visit(sg::point_light_t &point_light) override {
        renderer->add_point_light({
            .position = point_light.position,
            .color = point_light.color,
            .linear = point_light.linear,
            .quadratic = point_light.quadratic,
        });
    }
    void visit(sg::transform_t &transform) override {
        transform_stack.push_back(transform.get_local_matrix() * transform_stack.back());
        transform.accept_children(*this);
        transform_stack.pop_back();
    }
private:
    renderer_t *renderer;
    std::vector<m4f> transform_stack;
};

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

    g_model = g_loader.load_model({
        .path = model_path,
        .lod_settings = {
            { 10.0f, 4e-3f },
            { 20.0f, 1e-2f },
        }
    });

    auto tex_color1 = g_loader.load_texture({.path = "assets/tiles074_color.jpg"});
    // auto tex_color2 = g_loader.load_texture({.path = "assets/tiles133a_color.jpg"});
    // auto tex_color3 = g_loader.load_texture({.path = "assets/tiles081_color.jpg"});
    auto tex_normal = g_loader.load_texture({.path = "assets/tiles074_normal.jpg"});
    auto tex_roughness = g_loader.load_texture({.path = "assets/tiles074_roughness.jpg"});

    sg::scene_t scene = sg::scene_t();
    {
        auto t1 = scene.create_transform();
        t1->position = v3f{3, 0, 0};
        t1->scale = v3f{0.02, 0.02, 0.02};

        auto t2 = scene.create_transform();
        t2->position = v3f{0, 0, -3};

        scene.add(t1);
        scene.add(t2);

        auto g1 = scene.create_geometry(g_model->meshes[0]);
        t1->add(g1);
        t2->add(t1);

        auto l1 = scene.create_point_light();
        l1->position = v3f{0, 3, 0};
        l1->color = v3f{1, 1, 1};
        l1->linear = 0.09f;
        l1->quadratic = 0.032f;

        scene.add(l1);

        g_log.info("t1: {}", (void *)t1);
        g_log.info("g1: {}", (void *)g1);
        g_log.info("l1: {}", (void *)l1);

        // dump scene graph.
        log_dump_visitor_t dump_visitor;
        scene.accept(dump_visitor);
    }
    renderer_visitor_t visitor = renderer_visitor_t(&g_renderer);


    cpu_timer_t full_loop_timer;

    g_camera = camera_t(
        v3f{0, 0, 5}, v3f{0, 0, 0},
        m4f::perspective(anglef::from_deg(80.0f), 1200.0f / 900.0f, 0.01f, 20.0f)
    );
    freefly_controller_t controller;
    controller.camera = &g_camera;

    /*
    //u32 flags = (u32)draw_element_flags_t::use_albedo_tex | (u32)draw_element_flags_t::use_multi_tex;
    u32 flags = (u32)draw_element_flags_t::use_albedo_tex;
    draw_element_material_t material = {
        .flags = (draw_element_flags_t)flags,
        .color = v3f{0.3, 0.3, 0.3},
        .roughness = 0.5f,
        .metallic = 0.5f,
        .albedo0_idx = g_renderer.define_texture(tex_color1),
        // .albedo1_idx = g_renderer.define_texture(tex_color2),
        .albedo1_idx = 0,
        // .albedo2_idx = g_renderer.define_texture(tex_color3),
        .albedo2_idx = 0,
        .normal_idx = g_renderer.define_texture(tex_normal),
        .roughness_idx = g_renderer.define_texture(tex_roughness),
    };
    */

    float t = 0.0f;
    g_log.info("running...");
    while(!glfwWindowShouldClose(window)) {
        t += 0.017f;
        g_loader.process_hotreload();
        g_renderer.new_frame();

        full_loop_timer.start();

        controller.update(window, 0.16);

        gui();

        scene.accept(visitor);

        /*
        g_renderer.add_draw_indexed({
            .model = g_model.get(),
            .transform = m4f::translate(v3f{0, 0, 0}) * m4f::scale(v3f{scale, scale, scale}),
            .material = material,
            .lod = lod,
        });
        */

        // g_renderer.set_camera(g_camera);

        v3f lamp1_pos = v3f{4 * sin(t), 2, 4 * cos(t)};
        g_renderer.add_point_light({
            .position = lamp1_pos,
            .color = v3f{1, 1, 1},
            .linear = 0.09f,
            .quadratic = 0.032f,
        });


        g_renderer.add_point_light({
            .position = v3f{3, 2, 0},
            .color = v3f{1, 0.5, 0.3},
            .linear = 0.09f,
            .quadratic = 0.032f,
        });
        g_renderer.add_point_light({
            .position = v3f{-3, -1, 0},
            .color = v3f{0.3, 0.5, 1},
            .linear = 0.09f,
            .quadratic = 0.032f,
        });

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

void gui() {
    static bool show_imgui_demo = false;
    static bool show_implot_demo = false;
    if (show_imgui_demo)
        ImGui::ShowDemoWindow(&show_imgui_demo);

    if (show_implot_demo)
        ImPlot::ShowDemoWindow(&show_implot_demo);

    ImGui::Begin("test");

    /*
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
        ImGui::SliderInt("LOD", &lod_override, 0, mesh->meshes[0].lods.size() - 1);
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
        ImGui::Text("C Position: %.2f %.2f %.2f", g_camera.position.x, g_camera.position.y, g_camera.position.z);

        ImGui::DragFloat("Scale", &scale, 0.01f, 0.01f, 10.0f);
    }
     */

    if (ImGui::Button("Show ImGui demo")) show_imgui_demo = !show_imgui_demo;
    if (ImGui::Button("Show ImPlot demo")) show_implot_demo = !show_implot_demo;
    ImGui::End();
}

sg::node_t *interpret_node(tinyxml2::XMLElement *elem) {
    if (elem->Name() == std::string("group")) {
        sg::group_t *group = g_scene.create_group();
        for (tinyxml2::XMLElement *child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
            interpret_node(child);
        }

        return group;
    } else if (elem->Name() == std::string("geometry")) {
        sg::geometry_t *geometry = g_scene.create_geometry();

        return geometry;
    } else if (elem->Name() == std::string("point_light")) {
        sg::point_light_t *point_light = g_scene.create_point_light();

        return point_light;
    } else if (elem->Name() == std::string("transform")) {
        sg::transform_t *transform = g_scene.create_transform();
        for (tinyxml2::XMLElement *child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
            interpret_node(child);
        }

        return transform;
    } else {
        g_log.error("unknown node type: {}", elem->Name());
        return nullptr;
    }
}

void load_scene(const char *path) {
    // @todo: reset the current scene.

    tinyxml2::XMLDocument doc;
    doc.LoadFile(path);

    if (doc.Error()) {
        g_log.error("failed to load scene: {}", doc.ErrorStr());
        return;
    }

    tinyxml2::XMLElement *root = doc.FirstChildElement("scene");
    if (!root) {
        g_log.error("scene file does not contain a scene element");
        return;
    }

    for (tinyxml2::XMLElement *elem = root->FirstChildElement(); elem; elem = elem->NextSiblingElement()) {
        sg::node_t *node = interpret_node(elem);
        g_scene.add(node);
    }
}
