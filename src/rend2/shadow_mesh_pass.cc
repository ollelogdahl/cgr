#include "shadow_mesh_pass.h"
#include "rend2/pipeline_layout_builder.h"
#include "rend2/shader_compiler.h"
#include <vulkan/vulkan_core.h>

#include <tracy/Tracy.hpp>

VkPipeline build_pipeline(gpu_t &gpu, VkPipelineLayout layout, Shader &shader);

ShadowMeshPass::ShadowMeshPass(gpu_t &gpu, ShaderCompiler &sc,
    RenderState &state,
    GpuBuffer &draw_buffer)
: m_gpu(gpu), m_state(state),
    m_draw_buffer(draw_buffer),
    m_cull_pass(gpu, sc)
{
    m_cull_pass.bind_buffers(state.object_buffer(), draw_buffer, state.mesh_buffer());

    m_pipeline_layout = PipelineLayoutBuilder()
        .add_descriptor_set(state.render_descriptor_set().layout())
        .add_push_constant_range({VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m4f)})
        .build(gpu);

    // build up pipeline.
    Shader shader = Shader({
        sc.compile("shaders/shadow.vert"),
        sc.compile("shaders/empty.frag"),
    });
    m_pipeline = build_pipeline(gpu, m_pipeline_layout, shader);
}

void ShadowMeshPass::record(CommandBuffer &cmd, RenderTarget& target, const View& view,
    u32 max_count) {
    ZoneScoped;
    TracyVkZone(cmd.tracy_ctx(), cmd.get(), "shadow_mesh");

    // funky; vp matrix is inverted?
    m_cull_pass.update_view(view.position, view.view * view.projection);

    // bind the global descriptor set
    VkDescriptorSet descriptor_sets[] = { m_state.render_descriptor_set().get() };
    vkCmdBindDescriptorSets(cmd.get(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, 0,
        1, descriptor_sets, 0, nullptr);

    vkCmdBindIndexBuffer(cmd.get(), m_state.index_buffer().get(), 0, VK_INDEX_TYPE_UINT32);

    VkBuffer vertex_buffers[] = { m_state.vertex_buffer().get() };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(cmd.get(), 0, 1, vertex_buffers, offsets);

    VkViewport viewport = target.viewport();
    VkRect2D scissor = target.scissor();

    vkCmdSetViewport(cmd.get(), 0, 1, &viewport);
    vkCmdSetScissor(cmd.get(), 0, 1, &scissor);

    f32 bias_clamp = 0.0;
    if (m_gpu.support.depth_bias_clamp) {
        bias_clamp = m_bias_clamp;
    }

    vkCmdSetDepthBias(cmd.get(), m_bias_constant, m_bias_slope, bias_clamp);

    m4f vp = view.view * view.projection;
    vkCmdPushConstants(cmd.get(), m_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m4f), &vp);

    {
        TracyVkZone(cmd.tracy_ctx(), cmd.get(), "cull");
        m_cull_pass.record(cmd, m_state.highest_object_id() + 1);
    }

    WriteDependency cull_complete;
    cull_complete.add(
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_WRITE_BIT,
        m_draw_buffer.get());

    {
        TracyVkZone(cmd.tracy_ctx(), cmd.get(), "wait-cull-pass");
        cull_complete.pipeline_barrier(cmd.get(),
            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    }

    {
        TracyVkZone(cmd.tracy_ctx(), cmd.get(), "draw");

        VkRenderingAttachmentInfo depth_attachment = target.as_depth_attachment();

        VkRenderingInfo rendering_info{};
        rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering_info.renderArea = {
            .offset = {0, 0},
            .extent = target.extent
        };
        rendering_info.layerCount = 1;
        rendering_info.colorAttachmentCount = 0;
        rendering_info.pDepthAttachment = &depth_attachment;

        vkCmdBeginRendering(cmd.get(), &rendering_info);

        vkCmdBindPipeline(cmd.get(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

        vkCmdDrawIndexedIndirectCount(cmd.get(),
            m_draw_buffer.get(), sizeof(u32), m_draw_buffer.get(),
            0, max_count, sizeof(DrawCommand));

        vkCmdEndRendering(cmd.get());
    }
}

VkPipeline build_pipeline(gpu_t &gpu, VkPipelineLayout layout, Shader &shader) {
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
    rendering_info.colorAttachmentCount = 0;
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
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 0.000f; // Optional
        rasterizer.depthBiasClamp = 0.0f; // Optional
        rasterizer.depthBiasSlopeFactor = 0.00f; // Optional
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
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BIAS,
    };
    {
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = array_size(dynamic_states);
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
