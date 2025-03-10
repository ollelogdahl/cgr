#include "render.h"
#include "gpu.h"
#include "metrics.h"
#include "model.h"
#include "rend2/buffer.h"
#include "rend2/render_state.h"
#include "pipeline_layout_builder.h"
#include "descriptor_set_layout_builder.h"

#include <algorithm>
#include <vulkan/vulkan_core.h>

#include "rend2/shader_compiler.h"
#include "rend2/vku.h"
#include "tracy/Tracy.hpp"

class ImageDependency {
public:
    ImageDependency() = default;
    ImageDependency(VkImage image, VkPipelineStageFlags2 src_stage,
        VkAccessFlags2 src_access /* @todo: layouts */) {
        add(src_stage, src_access, image);
    }

    void add(VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkImage image) {
        barriers.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = src_stage,
            .srcAccessMask = src_access,
            .dstStageMask = 0,
            .dstAccessMask = 0,
            .image = image,
        });
    }

    void join(ImageDependency &other) {
        barriers.insert(barriers.end(), other.barriers.begin(), other.barriers.end());
    }

    void pipeline_barrier(VkCommandBuffer cmd, VkPipelineStageFlags2 dst_stage,
        VkAccessFlags2 dst_access) {
        for (auto &barrier : barriers) {
            barrier.dstStageMask = dst_stage;
            barrier.dstAccessMask = dst_access;
        }

        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = barriers.size();
        dependency.pImageMemoryBarriers = barriers.data();

        vkCmdPipelineBarrier2(cmd, &dependency);
    }
private:
    std::vector<VkImageMemoryBarrier2> barriers;
};

static const RenderStorageConfig config = {
    .max_objects = 20 * 1024,
    .max_vertices = 1 * 1024 * 1024,
    .max_indices = 1 * 1024 * 1024,
    .max_meshes = 1024,
    .max_materials = 20 * 1024,
    .max_textures = 1024,
    .max_lights = 1024,
};
static const u32 max_draws = config.max_objects;

static VkPipeline make_render_pipeline(gpu_t &gpu, VkPipelineLayout layout, Shader shader);

Renderer::Renderer(gpu_t &gpu, RenderStorage &storage, ShaderCompiler &sc) : m_gpu(&gpu), m_storage(storage),
    m_shader_compiler(sc),
    m_draw_buffer(gpu, max_draws * sizeof(DrawCommand) + 1 * sizeof(u32),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    m_forward_pass(gpu, sc, m_storage, m_draw_buffer),
    m_shadow_pass(gpu, sc, m_storage, m_draw_buffer) {

    m_forward_pipeline_layout = m_forward_pass.pipeline_layout();
}

RenderStorage::ShaderInfo Renderer::create_pipelines_for(Shader &shader) {
    auto render_pipeline = make_render_pipeline(*m_gpu, m_forward_pipeline_layout, shader);
    return {
        .render_pipeline = render_pipeline,
    };
}

void Renderer::render(gpu_t::frame_t &frame, const View &view) {
    ZoneScoped;

    m_storage.update_global({
        .view = view.view,
        .proj = view.projection,
        .view_pos = view.position,
    });

    auto state_dependencies = m_storage.flush(frame.cmd);

    // @todo: move this!
    // We invoke a compute shader which performs copies from the Object Buffer to
    // the Draw Buffer. It only copies if the objects are visible.
    // Therefore, we need to pass some cull information in a ubo or something.
    // This is actually recording to a different command buffer (and queue potentially)

    // @todo: consider different shaders! This is tricky. We want a DrawIndirect command for
    // each shader, and we don't want them to wait. Therefore, we need to either
    //      1. Partition the draw buffer by shader
    //      2. Have a separate draw buffer for each shader
    //
    // We maybe also should separate the object buffers by shader. This would make things
    // WAAAY easier i think. In that case, the culling and stuff does not need to care about
    // those details.
    {
        TracyVkZone(frame.cmd.tracy_ctx(), frame.cmd.get(), "wait-state-change");
        WriteDependency dependencies;
        dependencies.join(state_dependencies.objects);
        dependencies.join(state_dependencies.meshes);

        dependencies.pipeline_barrier(frame.cmd.get(),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    }

    // make one shadow pass for each light which casts shadows.
    /*
    ImageDependency shadow_dependencies;
    bool first_light = true;
    for (auto &light : m_state.lights()) {
        bool casts_shadow = light.shadowcast_texture_id != -1U;

        if (casts_shadow) {

            if (first_light) {
                first_light = false;
            } else {
                WriteDependency draw_buffer_dependency;
                draw_buffer_dependency.add(
                    VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                    VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                    m_draw_buffer.get());
                draw_buffer_dependency.pipeline_barrier(frame.cmd.get(),
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);
            }

            TextureSlot &slot = m_state.texture(light.shadowcast_texture_id);
            RenderTarget target = {
                .depth_view = slot.view,
                .extent = {2048, 2048},
                .clear_first = true,
            };

            f32 shadow_start_distance = 10.0f;
            f32 shadow_depth_distance = 20.0f;
            f32 shadow_width = 20.0f;

            v3f light_dir = light.position.xyz();

            m4f lookat = m4f::look_at(light_dir * -shadow_start_distance, {0, 0, 0}, {0, 1, 0});
            m4f projection = m4f::orthographic(-shadow_width, shadow_width, -shadow_width, shadow_width, 0.0001, shadow_depth_distance);

            View shadow_view = {
                .projection = projection,
                .view = lookat,
                .position = {0, 0, 0},
            };
            m_shadow_pass.record(frame.cmd, target, shadow_view, max_draws);

            shadow_dependencies.add(VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, slot.image);
        }
    }

    {
        // wait for shadow texture to be written.
        //  -> this happens after any writes to the draw buffer.

        TracyVkZone(frame.cmd.tracy_ctx(), frame.cmd.get(), "wait-shadow-pass");

        shadow_dependencies.pipeline_barrier(frame.cmd.get(),
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }
    */

    {
        RenderTarget target = {
            .color_view = m_gpu->swapchain.image_views[frame.image_idx],
            .depth_view = m_gpu->depth_image.view,
            .extent = m_gpu->swapchain.extent,
        };

        auto store_batches = m_storage.object_batches();
        std::vector<IndirectBatch2> batches(store_batches.size());
        for (size_t i = 0; i < store_batches.size(); i++) {
            auto &batch = store_batches[i];
            batches[i] = {
                .pipeline = m_storage.shaders()[batch.id].render_pipeline,
                .buffer_offset = batch.start_index,
                .count = batch.count,
            };
        }

        m_forward_pass.record(frame.cmd, target, view, batches);
    }
}

bool operator==(const LoadShaderProperties &lhs, const LoadShaderProperties &rhs) {
    return lhs.glsl_vert_path == rhs.glsl_vert_path && lhs.glsl_frag_path == rhs.glsl_frag_path;
}

std::size_t std::hash<LoadShaderProperties>::operator()(const LoadShaderProperties &props) const {
    return std::hash<const char *>()(props.glsl_vert_path) ^ std::hash<const char *>()(props.glsl_frag_path);
}

bool operator==(const LoadTextureProperties &lhs, const LoadTextureProperties &rhs) {
    return lhs.path == rhs.path && lhs.type == rhs.type;
}

std::size_t std::hash<LoadTextureProperties>::operator()(const LoadTextureProperties &props) const {
    return std::hash<const char *>()(props.path) ^ std::hash<u32>()(static_cast<u32>(props.type));
}

VkPipeline make_render_pipeline(gpu_t &gpu, VkPipelineLayout layout, Shader shader) {
    std::vector<VkFormat> color_attachment_formats = {
        VK_FORMAT_B8G8R8A8_UNORM
    };
    VkFormat depth_attachment_format = VK_FORMAT_D32_SFLOAT;

    std::vector<VkVertexInputBindingDescription> bindings = {
        {
            .binding = 0,
            .stride = 9 * sizeof(f32),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        }
    };
    std::vector<VkVertexInputAttributeDescription> attributes = {
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
        },
        {
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = 8 * sizeof(f32),
        }
    };

    VkBool32 depth_test = VK_TRUE;
    VkBool32 depth_write = VK_TRUE;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;

    // boilerplate
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f; // Optional
    multisampling.pSampleMask = nullptr; // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE; // Optional

    VkPipelineRenderingCreateInfoKHR rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    rendering_info.colorAttachmentCount = color_attachment_formats.size();
    rendering_info.pColorAttachmentFormats = color_attachment_formats.data();
    rendering_info.depthAttachmentFormat = depth_attachment_format;
    rendering_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineViewportStateCreateInfo viewport_state{};
    {
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;
    }

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    {
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE; // useful to set as true for shadow mapping
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f; // Optional
        rasterizer.depthBiasClamp = 0.0f; // Optional
        rasterizer.depthBiasSlopeFactor = 0.0f; // Optional
    }

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY; // Optional
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &colorBlendAttachment;
    color_blending.blendConstants[0] = 0.0f; // Optional
    color_blending.blendConstants[1] = 0.0f; // Optional
    color_blending.blendConstants[2] = 0.0f; // Optional
    color_blending.blendConstants[3] = 0.0f; // Optional

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering_info;
    shader.apply_to(pipeline_info);

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    {
        vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input_info.vertexBindingDescriptionCount = bindings.size();
        vertex_input_info.pVertexBindingDescriptions = bindings.data();
        vertex_input_info.vertexAttributeDescriptionCount = attributes.size();
        vertex_input_info.pVertexAttributeDescriptions = attributes.data();

        pipeline_info.pVertexInputState = &vertex_input_info;
    }

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    {
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = 2;
        dynamic_state.pDynamicStates = dynamic_states;
    }

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state{};
    {
        depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil_state.depthTestEnable = depth_test;
        depth_stencil_state.depthWriteEnable = depth_write;
        depth_stencil_state.depthCompareOp = depth_compare_op;
        depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
        depth_stencil_state.minDepthBounds = 0.0f;
        depth_stencil_state.maxDepthBounds = 1.0f;
    }

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil_state;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = layout;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(gpu.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) != VK_SUCCESS) {
        // gpu_log.error("failed to create graphics pipeline");
    }

    return pipeline;
}
