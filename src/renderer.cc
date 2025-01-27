#include "renderer.h"
#include "gpu.h"

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

void renderer_t::init(gpu_t &gpu, loader_t &loader) {
    this->gpu = &gpu;

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
        }

        const VkPushConstantRange ranges[2] = {
            { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m4f) },
            { VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(material_push_block_t), sizeof(m4f) }
        };

        auto shader = loader.load_shader_program({
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
}

texhnd_t renderer_t::define_texture(ref_t<texture_t> texture) {
    defined_textures.push_back(texture);
    u32 id = defined_textures.size() - 1;

    descriptor_writer_t writer;
    // writer.write_combined_image_sampler(0, id, texture->image.view, gpu->sampler);
    writer.update_set(*gpu, texture_descriptor_set);

    return id;
}

void renderer_t::add_element(draw_element_t &element) {
    draw_elements.push_back(element);
}
void renderer_t::add_point_light(pl_element_t &element) {
    point_lights.push_back(element);
}
void renderer_t::set_camera(camera_t &camera) {
    this->camera = &camera;
}

void renderer_t::new_frame() {
    draw_elements.clear();
    point_lights.clear();
    camera = nullptr;
}
void renderer_t::draw(gpu_t::frame_t &frame) {
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
    VkDescriptorSet sets[] = { descriptor_set, texture_descriptor_set };
    vkCmdBindDescriptorSets(frame.cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout,
        0, array_size(sets), sets, 0, nullptr);

    for (auto &element : draw_elements) {
        m4f transform = element.transform;
        u32 flags = 0;
        flags |= (element.material.use_albedo_tex << 0);
        flags |= (element.material.use_normal_tex << 1);
        flags |= (element.material.use_roughness_tex << 2);

        material_push_block_t push_block = {
            .flags = flags,
            .color_r = element.material.color_r,
            .color_g = element.material.color_g,
            .color_b = element.material.color_b,
            .roughness = element.material.roughness,
            .metallic = element.material.metallic,
            .albedo_tex_idx = element.material.albedo_tex_idx,
            .normal_tex_idx = element.material.normal_tex_idx,
            .roughness_tex_idx = element.material.roughness_tex_idx,
            .padding = {0}
        };

        vkCmdPushConstants(frame.cmds, pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m4f), &transform);
        vkCmdPushConstants(frame.cmds, pipeline->layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(m4f), sizeof(material_push_block_t), &push_block);

        VkBuffer buffers[] = {element.vertex_buffer->handle};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(frame.cmds, 0, 1, buffers, offsets);
        vkCmdBindIndexBuffer(frame.cmds, element.index_buffer->handle, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(frame.cmds, element.index_count, 1, element.index_start, 0, 0);
    }

    vkCmdEndRendering(frame.cmds);
}
