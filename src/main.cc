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
#include "resource.h"
#include "camera.h"
#include "freefly_controller.h"
#include "vks.h"

#define MAX_FRAMES_IN_FLIGHT 3

loader_t g_loader;

void imgui_init(gpu_t &gpu) {
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // setup implot style
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, {0.15,0.15,0.15,0.0});

    VkDescriptorPool descriptor_pool;
    {
        VkDescriptorPoolSize imgui_pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = array_size(imgui_pool_sizes);
        pool_info.pPoolSizes = imgui_pool_sizes;
        if (vkCreateDescriptorPool(gpu.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool");
        }
    }

    // create unique gpu stuffs for imgui...
    // @todo: recreate this when swapchain is recreated ? HMM.
    ImGui_ImplGlfw_InitForVulkan(gpu.window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = gpu.instance;
    init_info.PhysicalDevice = gpu.pdev;
    init_info.Device = gpu.device;
    init_info.QueueFamily = gpu.queue_families.graphics,
    init_info.Queue = gpu.graphics_queue,
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = descriptor_pool;
    init_info.UseDynamicRendering = true;
    init_info.Subpass = 0;
    init_info.MinImageCount = gpu.swapchain.image_count;
    init_info.ImageCount = gpu.swapchain.image_count;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = VK_NULL_HANDLE;
    init_info.CheckVkResultFn = nullptr;

    init_info.PipelineRenderingCreateInfo = {};
    init_info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &gpu.swapchain.image_format;
	init_info.PipelineRenderingCreateInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

    ImGui_ImplVulkan_Init(&init_info);
}

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

struct material_push_block_t {
    u32 flags;
    f32 color_r, color_g, color_b;
    f32 roughness;
    f32 metallic;

    u32 albedo_tex_idx;
    u32 normal_tex_idx;
    u32 roughness_tex_idx;

    u32 padding[5];
};

#include <stb/stb_image.h>

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

    imgui_init(gpu);
    g_log.info("imgui initialized");

    auto mesh = g_loader.load_model({
        .path = "assets/arena.obj",
        .lod_settings = {
            { 10.0f, 4e-3f },
            { 20.0f, 1e-2f },
        }
    });

    auto tex_color = g_loader.load_texture({.path = "assets/img0.jpg"});
    auto tex_normal = g_loader.load_texture({.path = "assets/tiles074_normal.jpg"});
    auto tex_roughness = g_loader.load_texture({.path = "assets/tiles074_roughness.jpg"});

    // create a test pipeline and pass
    ref_t<gpu_pipeline_t> pipeline;
    VkDescriptorSetLayout descriptorSetLayout1;
    VkDescriptorSetLayout descriptorSetLayout2;
    {
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f; // Optional
        multisampling.pSampleMask = nullptr; // Optional
        multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
        multisampling.alphaToOneEnable = VK_FALSE; // Optional

        // @todo: temp
        {
            VkDescriptorSetLayoutBinding uboLayoutBinding{};
            uboLayoutBinding.binding = 0;
            uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboLayoutBinding.descriptorCount = 1;
            uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &uboLayoutBinding;

            if (vkCreateDescriptorSetLayout(gpu.device, &layoutInfo, nullptr, &descriptorSetLayout1) != VK_SUCCESS) {
                throw std::runtime_error("failed to create descriptor set layout!");
            }
        }

        {
            VkDescriptorBindingFlags binding_flags[] = {
                VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
            };

            VkDescriptorSetLayoutBinding layout_bindings[] = {
                VkDescriptorSetLayoutBinding{
                    .binding = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 65536,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = nullptr,
                },
            };

            VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_create_info{};
            binding_flags_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            binding_flags_create_info.bindingCount = array_size(layout_bindings);
            binding_flags_create_info.pBindingFlags = binding_flags;

            VkDescriptorSetLayoutCreateInfo create_info{};
            create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            create_info.pNext = &binding_flags_create_info;
            create_info.bindingCount = array_size(layout_bindings);
            create_info.pBindings = layout_bindings;

            if (vkCreateDescriptorSetLayout(gpu.device, &create_info, nullptr, &descriptorSetLayout2) != VK_SUCCESS) {
                throw std::runtime_error("failed to create descriptor set layout!");
            }

            g_log.info("bindless set layout: {}", (void *)descriptorSetLayout2);
            for (auto &b : layout_bindings) {
                g_log.info("    [{}]: {}", b.binding, b.descriptorCount);
            }
        }

        const VkPushConstantRange ranges[2] = {
            { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m4f) },
            { VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(material_push_block_t), sizeof(m4f) }
        };

        auto shader = g_loader.load_shader_program({
           .vertex_hlsl_path = "eassets/shaders/test.vert",
           .fragment_hlsl_path = "eassets/shaders/test.frag",
        });

        // @todo: It would be fun to try to de-interlace the properties.
        pipeline = gpu.make_pipeline({
            .shader = shader,
            .layout = {
                .flags = 0,
                .descriptor_set_layouts = { descriptorSetLayout1, descriptorSetLayout2 },
                .push_constant_ranges = { ranges[0], ranges[1] },
            },
            .vertex_input_info = {
                .bindings = {
                    {
                        .binding = 0,
                        .stride = 8 * sizeof(f32),
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                    }
                },
                .attributes = {
                    {
                        .location = 0,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = 0,
                    },
                    {
                        .location = 1,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = 3 * sizeof(f32),
                    },
                    {
                        .location = 2,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32_SFLOAT,
                        .offset = 6 * sizeof(f32),
                    }

                },
            },
            .depth_stencil = {
                .depth_test = true,
                .depth_write = true,
                .depth_compare_op = VK_COMPARE_OP_LESS,
            },
            .multisampling = multisampling,
            .color_attachment_formats = { VK_FORMAT_B8G8R8A8_UNORM },
            .depth_attachment_format = VK_FORMAT_D32_SFLOAT,
        });
    }

    cpu_timer_t full_loop_timer;
    gpu_timer_t full_frame_timer;
    gpu_timer_t imgui_render_timer;

    full_frame_timer.init(gpu);
    imgui_render_timer.init(gpu);

    vkDeviceWaitIdle(gpu.device);

    struct env_ubo_t {
        m4f view;
        m4f proj;
        v3f view_pos;
    };

    camera_t camera = camera_t(
        v3f{0, 0, 5}, v3f{0, 0, 0},
        m4f::perspective(anglef::from_deg(80.0f), 1200.0f / 900.0f, 0.01f, 20.0f)
    );
    freefly_controller_t controller;
    controller.camera = &camera;

    gpu_buffer_t env_ubo_buffer;
    gpu.create_buffer(sizeof(env_ubo_t), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        env_ubo_buffer);

    // we will not be using UPDATE_AFTER_BIND.
    VkDescriptorPool descriptor_pool;
    {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 65536 }
        };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = array_size(pool_sizes);
        poolInfo.pPoolSizes = pool_sizes;
        poolInfo.maxSets = 16;

        VK_CHECK(vkCreateDescriptorPool(gpu.device, &poolInfo, nullptr, &descriptor_pool));
    }

    VkDescriptorSet descriptor_set;
    VkDescriptorSet bindless_descriptor_set;
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptor_pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout1;

        VK_CHECK(vkAllocateDescriptorSets(gpu.device, &allocInfo, &descriptor_set));
    }

    {
        u32 counts[] = { 65536 };
        VkDescriptorSetVariableDescriptorCountAllocateInfo variable_descriptor_count_info = {};
        variable_descriptor_count_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        variable_descriptor_count_info.descriptorSetCount = array_size(counts);
        variable_descriptor_count_info.pDescriptorCounts = counts;

        VkDescriptorSetAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.pNext = &variable_descriptor_count_info;
        alloc_info.descriptorPool = descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &descriptorSetLayout2;

        VK_CHECK(vkAllocateDescriptorSets(gpu.device, &alloc_info, &bindless_descriptor_set));
    }



    // upload some textures to the bindless descriptor set
    {
        VkSampler sampler;
        {
            VkSamplerCreateInfo sampler_info = {};
            sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sampler_info.magFilter = VK_FILTER_LINEAR;
            sampler_info.minFilter = VK_FILTER_LINEAR;
            sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler_info.anisotropyEnable = VK_FALSE;
            sampler_info.maxAnisotropy = 1.0f;
            sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            sampler_info.unnormalizedCoordinates = VK_FALSE;
            sampler_info.compareEnable = VK_FALSE;
            sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
            sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            sampler_info.mipLodBias = 0.0f;
            sampler_info.minLod = 0.0f;
            sampler_info.maxLod = 0.0f;

            VK_CHECK(vkCreateSampler(gpu.device, &sampler_info, nullptr, &sampler));
        }

        auto writer = descriptor_writer_t();
        writer.write_combined_image_sampler(0, 0, tex_color->image.view, sampler);
        writer.write_combined_image_sampler(0, 1, tex_normal->image.view, sampler);
        writer.write_combined_image_sampler(0, 2, tex_roughness->image.view, sampler);
        writer.update_set(gpu, bindless_descriptor_set);
    }

    {
        auto writer = descriptor_writer_t();
        writer.write_buffer(0, 0, env_ubo_buffer.handle, 0, sizeof(env_ubo_t));
        writer.update_set(gpu, descriptor_set);
    }

    material_push_block_t material = {
        .flags = 1,
        .color_r = 0.4f,
        .color_g = 0.7f,
        .color_b = 0.7f,
        .roughness = 0.5f,
        .metallic = 0.0f,
        .albedo_tex_idx = 0,
        .normal_tex_idx = 1,
        .roughness_tex_idx = 2,
        .padding = {0}
    };

    v3f scale = {0.02, 0.02, 0.02};
    bool lod_override = false;
    i32 lod_override_value = 0;

    g_log.info("running...");
    while(!glfwWindowShouldClose(window)) {
        g_loader.process_hotreload();
        full_loop_timer.start();

        controller.update(window, 0.16);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

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
            ImGui::ColorEdit3("Color", &material.color_r);
            ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f);
            ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f);

            ImGui::CheckboxFlags("Use Albedo Texture", &material.flags, 1);
            ImGui::CheckboxFlags("Use Normal Texture", &material.flags, 2);
            ImGui::CheckboxFlags("Use Roughness Texture", &material.flags, 4);
        }

        {
            ImGui::SeparatorText("Transform");
            ImGui::SliderFloat3("Scale", &scale.x, 0.0f, 1.0f);
        }

        if (ImGui::Button("Show ImGui demo")) show_imgui_demo = !show_imgui_demo;
        if (ImGui::Button("Show ImPlot demo")) show_implot_demo = !show_implot_demo;


        ImGui::End();
        ImGui::Render();

        env_ubo_t env_ubo = {
            .view = camera.view_matrix,
            .proj = camera.projection_matrix,
            .view_pos = camera.position,
        };
        gpu.write_buffer(env_ubo_buffer, slice<u8>((u8 *)&env_ubo, sizeof(env_ubo_t)));

        gpu.frame([&](gpu_t::frame_t &frame) {

            VkRenderingAttachmentInfo color_attachments[] = {
                {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .pNext = nullptr,
                    .imageView = gpu.swapchain.image_views[frame.image_idx],
                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .resolveMode = VK_RESOLVE_MODE_NONE,
                    .resolveImageView = VK_NULL_HANDLE,
                    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}},
                }
            };
            VkRenderingAttachmentInfo depth_attachment = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .pNext = nullptr,
                .imageView = gpu.depth_image.view,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_NONE,
                .resolveImageView = VK_NULL_HANDLE,
                .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {{{1.0f, 0}}},
            };

            VkRenderingInfo rendering_info{};
            rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering_info.renderArea = {
                .offset = {0, 0},
                .extent = gpu.swapchain.extent
            };
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = array_size(color_attachments);
            rendering_info.pColorAttachments = color_attachments;
            rendering_info.pDepthAttachment = &depth_attachment;

            vkCmdBeginRendering(frame.cmds, &rendering_info);

            vkCmdBindPipeline(frame.cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

            VkViewport viewport{
                .x = 0.0f,
                .y = 0.0f,
                .width = (f32)gpu.swapchain.extent.width,
                .height = (f32)gpu.swapchain.extent.height,
                .minDepth = 0.0f,
                .maxDepth = 1.0f,
            };
            VkRect2D scissor{
                .offset = {0, 0},
                .extent = gpu.swapchain.extent,
            };

            vkCmdSetViewport(frame.cmds, 0, 1, &viewport);
            vkCmdSetScissor(frame.cmds, 0, 1, &scissor);
            VkDescriptorSet sets[] = { descriptor_set, bindless_descriptor_set };
            vkCmdBindDescriptorSets(frame.cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout,
                0, array_size(sets), sets, 0, nullptr);


            for (auto &m : mesh->meshes) {
                auto lod = 0;

                // we need to get the distance from the camera to the object. This
                // is used for lod selection.
                if(lod_override) {
                    lod = lod_override_value;
                }

                m4f transform = m4f::translate(m.bounds.center()) * m4f::scale(scale) * m4f::identity();
                vkCmdPushConstants(frame.cmds, pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m4f), &transform);
                vkCmdPushConstants(frame.cmds, pipeline->layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(m4f), sizeof(material_push_block_t), &material);

                VkBuffer buffers[] = {m.vertex_buffer.handle};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(frame.cmds, 0, 1, buffers, offsets);

                vkCmdBindIndexBuffer(frame.cmds, m.lods[lod].index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(frame.cmds, m.lods[lod].index_count, 1, 0, 0, 0);
            }

            // imgui should probably be rendered with another rendering.
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.cmds);

            vkCmdEndRendering(frame.cmds);
        });

        glfwPollEvents();

        full_loop_timer.stop();
    }

    glfwDestroyWindow(window);

    glfwTerminate();
}
