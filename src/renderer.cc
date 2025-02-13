#include "renderer.h"
#include "gpu.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_vulkan.h"
#include "implot/implot.h"
#include "log.h"
#include <variant>

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

enum struct draw_indexed_flags_t {
    none = 0,
    use_albedo_tex = 1 << 0,
    use_normal_tex = 1 << 1,
    use_roughness_tex = 1 << 2,
    use_multi_tex = 1 << 3,
};

struct env_ubo_t {
    m4f view;
    m4f proj;
    v3f view_pos;
    u32 num_point_lights;
    u32 num_dir_lights;
    f32 _pad[3];

    struct {
        v3f position;
        f32 linear;
        v3f color;
        f32 quadratic;
    } point_lights[16];

    struct {
        v3f direction;
        f32 _pad1;
        v3f color;
        f32 _pad2;
    } dir_lights[16];
};

struct material_push_block_t {
    u32 flags;
    f32 color_r, color_g, color_b;
    f32 roughness;
    f32 metallic;

    texhnd_t albedo0_idx;
    texhnd_t albedo1_idx;
    texhnd_t albedo2_idx;

    texhnd_t normal_idx;
    texhnd_t roughness_idx;

    u32 padding[1] = {0};
};

void imgui_init(gpu_t &gpu);

void renderer_t::init(gpu_t &gpu, loader_t &loader) {
    this->gpu = &gpu;

    imgui_init(gpu);

    // setup pipeline
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

            if (vkCreateDescriptorSetLayout(gpu.device, &layoutInfo, nullptr, &this->main_descriptor_set_layout) != VK_SUCCESS) {
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

            if (vkCreateDescriptorSetLayout(gpu.device, &create_info, nullptr, &this->texture_descriptor_set_layout) != VK_SUCCESS) {
                throw std::runtime_error("failed to create descriptor set layout!");
            }
        }

        const VkPushConstantRange ranges[2] = {
            { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m4f) },
            { VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(material_push_block_t), sizeof(m4f) }
        };

        auto shader = loader.load_shader_program({
           .vertex_hlsl_path = "shaders/test.vert",
           .fragment_hlsl_path = "shaders/test.frag",
        });

        // @todo: It would be fun to try to de-interlace the properties.
        pipeline = gpu.make_pipeline({
            .shader = shader,
            .layout = {
                .flags = 0,
                .descriptor_set_layouts = { main_descriptor_set_layout, texture_descriptor_set_layout },
                .push_constant_ranges = { ranges[0], ranges[1] },
            },
            .vertex_input_info = {
                .bindings = {
                    {
                        .binding = 0,
                        .stride = 9 * sizeof(f32),
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
                        .offset = 7 * sizeof(f32),
                    },
                    {
                        .location = 3,
                        .binding = 0,
                        .format = VK_FORMAT_R8G8B8A8_UNORM,
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

    // setup descriptor sets
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
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptor_pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &main_descriptor_set_layout;

        VK_CHECK(vkAllocateDescriptorSets(gpu.device, &allocInfo, &main_descriptor_set));
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
        alloc_info.pSetLayouts = &texture_descriptor_set_layout;

        VK_CHECK(vkAllocateDescriptorSets(gpu.device, &alloc_info, &texture_descriptor_set));
    }

    // create the shared sampler
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

        VK_CHECK(vkCreateSampler(gpu.device, &sampler_info, nullptr, &shared_sampler));
    }

    {
        // create the ubo buffer.
        gpu.create_buffer(sizeof(env_ubo_t),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            env_ubo_buffer);
    }

    {
        // populate main descriptor set
        auto writer = descriptor_writer_t();
        writer.write_buffer(0, 0, env_ubo_buffer.handle, 0, sizeof(env_ubo_t));
        writer.update_set(gpu, main_descriptor_set);
    }
}

texhnd_t renderer_t::get_or_create_texture_handle(ref_t<texture_t> texture) {
    if (texture == nullptr) {
        return -1U;
    }

    auto it = textures.textures.find(texture);
    if (it != textures.textures.end()) {
        return it->second;
    }

    texhnd_t id;
    if (textures.free_slots.size() > 0) {
        id = textures.free_slots.back();
        textures.free_slots.pop_back();
    } else {
        id = textures.slots.size();
        textures.slots.push_back(texture);
    }

    textures.textures[texture] = id;

    textures.writer.write_combined_image_sampler(0, id, texture->image.view, shared_sampler);
    textures.has_updated = true;

    return id;
}

void renderer_t::add_draw_indexed(const draw_indexed_command_t &cmd) {
    commands.draw_indexed.push_back(cmd);
}

void renderer_t::add_point_light(const pl_command_t &element) {
    commands.point_lights.push_back(element);
}

void renderer_t::add_directional_light(const dl_command_t &cmd) {
    commands.directional_lights.push_back(cmd);
}

void renderer_t::set_camera(camera_t &camera) {
    this->camera = &camera;
}

void renderer_t::new_frame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // reset the draw elements
    commands.clear();
    camera = nullptr;
}

material_push_block_t make_material_push_block(switch_material_op_t &op);

void renderer_t::update_frame_data() {
    // save off imgui
    ImGui::Render();

    // write to ubo buffers.
    // @todo: it is expensive to update the whole buffer all the time.
    // we can probably be smarter about this.
    env_ubo_t ubo = {};
    ubo.view = camera->view_matrix;
    ubo.proj = camera->projection_matrix;
    ubo.view_pos = camera->position;
    ubo.num_point_lights = commands.point_lights.size();
    ubo.num_dir_lights = commands.directional_lights.size();

    for (usize i = 0; i < commands.point_lights.size(); ++i) {
        ubo.point_lights[i].position = commands.point_lights[i].position;
        ubo.point_lights[i].color = commands.point_lights[i].color;
        ubo.point_lights[i].linear = commands.point_lights[i].linear;
        ubo.point_lights[i].quadratic = commands.point_lights[i].quadratic;
    }

    for (usize i = 0; i < commands.directional_lights.size(); ++i) {
        ubo.dir_lights[i].direction = commands.directional_lights[i].direction;
        ubo.dir_lights[i].color = commands.directional_lights[i].color;
    }
    auto ubo_slice = slice<u8>((u8 *)&ubo, sizeof(ubo));

    gpu->write_buffer_with_barrier(env_ubo_buffer, ubo_slice, ubo_write_barrier);

    // write textures if changed
    if (textures.has_updated) {
        textures.writer.update_set(*gpu, texture_descriptor_set);
        textures.has_updated = false;
    }
}
void renderer_t::draw(gpu_t::frame_t &frame) {

    // wait for the ubo to be written.
    ubo_write_barrier.set_dst(
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT);
    vkCmdPipelineBarrier2(frame.cmds,
        &ubo_write_barrier.dependency_info);


    VkRenderingAttachmentInfo color_attachments[] = {
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext = nullptr,
            .imageView = gpu->swapchain.image_views[frame.image_idx],
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
        .imageView = gpu->depth_image.view,
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
        .extent = gpu->swapchain.extent
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
        .width = (f32)gpu->swapchain.extent.width,
        .height = (f32)gpu->swapchain.extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor{
        .offset = {0, 0},
        .extent = gpu->swapchain.extent,
    };

    vkCmdSetViewport(frame.cmds, 0, 1, &viewport);
    vkCmdSetScissor(frame.cmds, 0, 1, &scissor);
    VkDescriptorSet sets[] = { main_descriptor_set, texture_descriptor_set };
    vkCmdBindDescriptorSets(frame.cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout,
        0, array_size(sets), sets, 0, nullptr);


    // @todo: implement instancing.
    // this is actually really simple. If we can sort the elements by the mesh,
    // we can just bind the vertex buffer once, and then draw all the instances
    // of that mesh.
    //
    // the problem is that the material is defined as a push constant, so we cannot
    // draw the same mesh with different materials. But that is probably unusual.
    //
    // I think this is a viable route.

    // @todo: implement different
    auto planned_ops = planner.plan_rendering(*this);
    for (auto &op : planned_ops) {
        std::visit(overloaded{
            [&](switch_buffers_op_t &op) {
                VkBuffer buffers[] = {op.vertex_buffer->handle};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(frame.cmds, 0, 1, buffers, offsets);
                vkCmdBindIndexBuffer(frame.cmds, op.index_buffer->handle, 0, VK_INDEX_TYPE_UINT32);
            },
            [&](draw_indexed_op_t &op) {
                vkCmdPushConstants(frame.cmds, pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m4f), &op.transform);
                vkCmdDrawIndexed(frame.cmds, op.index_count, 1, op.index_offset, op.vertex_offset, 0);
            },
            [&](switch_material_op_t &op) {
                auto push_block = make_material_push_block(op);

                vkCmdPushConstants(frame.cmds, pipeline->layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(m4f), sizeof(material_push_block_t), &push_block);
            },
        }, op);
    }

    // @todo: move imgui into separate pass
    {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.cmds);
    }

    vkCmdEndRendering(frame.cmds);
}

// @todo: probably doesn't belong here. Its fine for now.
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
    // @todo: technically, we should recreate this on swapchain recreation. I think.
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

material_push_block_t make_material_push_block(switch_material_op_t &op) {
    u32 flags = 0;

    if (op.albedo0_idx != -1U) {
        flags |= (u32)draw_indexed_flags_t::use_albedo_tex;
    }
    if (op.albedo1_idx != -1U) {
        flags |= (u32)draw_indexed_flags_t::use_multi_tex;
    }
    if (op.albedo2_idx != -1U) {
        flags |= (u32)draw_indexed_flags_t::use_multi_tex;
    }
    if (op.normal_idx != -1U) {
        flags |= (u32)draw_indexed_flags_t::use_normal_tex;
    }
    if (op.roughness_idx != -1U) {
        flags |= (u32)draw_indexed_flags_t::use_roughness_tex;
    }

    return {
        .flags = flags,
        .color_r = op.material->diffuse.x,
        .color_g = op.material->diffuse.y,
        .color_b = op.material->diffuse.z,
        .roughness = op.material->roughness,
        .metallic = op.material->metallic,
        .albedo0_idx = op.albedo0_idx,
        .albedo1_idx = op.albedo1_idx,
        .albedo2_idx = op.albedo2_idx,
        .normal_idx = op.normal_idx,
        .roughness_idx = op.roughness_idx,
    };
}
