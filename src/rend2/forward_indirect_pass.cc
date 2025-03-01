#include "forward_indirect_pass.h"
#include "metrics.h"
#include "rend2/descriptor_set_layout_builder.h"
#include "rend2/pipeline_layout_builder.h"
#include "rend2/render.h"
#include "rend2/render_state.h"
#include "rend2/shader_compiler.h"
#include <vulkan/vulkan_core.h>

#include <tracy/Tracy.hpp>

VkPipeline tmp_create_graphics_pipeline(gpu_t &gpu, VkPipelineLayout layout, const Shader &shader);

const char *pipeline_stat_names[] = {
    "rend2.forward.input_assembly_vertices",
    "rend2.forward.input_assembly_primitives",
    "rend2.forward.vertex_shader_invocations",
    "rend2.forward.clipping_invocations",
    "rend2.forward.clipping_primitives",
    "rend2.forward.fragment_shader_invocations",
};

ForwardIndirectPass::ForwardIndirectPass(gpu_t &gpu, ShaderCompiler &sc, u32 max_textures) : m_gpu(&gpu), m_query(gpu) {

    // create a descriptor pool
    VkDescriptorPool descriptor_pool;
    {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_textures },
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = array_size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = 1;

        VK_CHECK(vkCreateDescriptorPool(gpu.device, &pool_info, nullptr, &descriptor_pool));
    }

    // create the descriptor set layout
    //     binding 0: global buffer
    //     binding 1: object buffer
    //     binding 2: material buffer
    //     binding 3: texture descriptor array
    VkDescriptorSetLayout ds_layout = DescriptorSetLayoutBuilder()
        .add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
        .add_binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_SHADER_STAGE_FRAGMENT_BIT)
        .add_variable_binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, max_textures)
        .build(gpu);

    m_descriptor_set.init(gpu, descriptor_pool, ds_layout);

    m_pipeline_layout = PipelineLayoutBuilder()
        .add_descriptor_set(ds_layout)
        .build(gpu);

    Shader shader = Shader({
        sc.compile("shaders/forward.vert"),
        sc.compile("shaders/forward.frag")
    });

    m_pipeline = tmp_create_graphics_pipeline(gpu, m_pipeline_layout, shader);
}

void ForwardIndirectPass::update_textures(std::span<TextureWrite> writes) {
    for (auto &write : writes) {
        m_descriptor_set.write_combined_image_sampler(3, write.index, write.view, write.sampler);
    }
    m_descriptor_set.flush(*m_gpu);
}

void ForwardIndirectPass::set_resources(VkBuffer global_buffer, VkBuffer object_buffer,
    VkBuffer draw_buffer, VkBuffer material_buffer) {

    m_draw_buffer = draw_buffer;

    // write the descriptor set
    m_descriptor_set.write_storage_buffer(0, 0, global_buffer, 0, VK_WHOLE_SIZE);
    m_descriptor_set.write_storage_buffer(1, 0, object_buffer, 0, VK_WHOLE_SIZE);
    m_descriptor_set.write_storage_buffer(2, 0, material_buffer, 0, VK_WHOLE_SIZE);
    m_descriptor_set.flush(*m_gpu);
}

void ForwardIndirectPass::record(CommandBuffer &cmd, const RenderTarget &target, std::span<IndirectBatch> batches) {
    ZoneScoped;
    TracyVkZone(cmd.tracy_ctx(), cmd.get(), "forward_indirect");

    auto q_results = m_query.get_results();
    for (u32 i = 0; i < array_size(pipeline_stat_names); ++i) {
        metrics::gauge_u64(pipeline_stat_names[i], q_results[i]);
    }

    m_query.begin(cmd);

    // bind the global descriptor set
    VkDescriptorSet descriptor_sets[] = { m_descriptor_set.get() };
    vkCmdBindDescriptorSets(cmd.get(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, 0,
        1, descriptor_sets, 0, nullptr);

    // begin render pass
    // @todo: a forward pass 'could' render somewhere else.
    VkRenderingAttachmentInfo color_attachments[] = {
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext = nullptr,
            .imageView = target.color_view,
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
        .imageView = target.depth_view,
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
        .extent = target.extent
    };
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = array_size(color_attachments);
    rendering_info.pColorAttachments = color_attachments;
    rendering_info.pDepthAttachment = &depth_attachment;

    vkCmdBeginRendering(cmd.get(), &rendering_info);

    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = (f32)target.extent.width,
        .height = (f32)target.extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor{
        .offset = {0, 0},
        .extent = target.extent,
    };

    vkCmdSetViewport(cmd.get(), 0, 1, &viewport);
    vkCmdSetScissor(cmd.get(), 0, 1, &scissor);

    // @todo: bind pipelines 4 real
    vkCmdBindPipeline(cmd.get(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    metrics::gauge_u64("rend2.forward.batches", batches.size());
    // for each batch, perform a bindless
    for (auto &batch : batches) {
        // remember that the draw-buffer is laid out like
        // [count][draw0     ][draw1      ]...[drawN      ]
        // u32    DrawCmd     DrawCmd
        vkCmdDrawIndexedIndirectCount(cmd.get(),
            m_draw_buffer, batch.index * sizeof(DrawCommand) + sizeof(u32), m_draw_buffer, 0, batch.max_draws,
            sizeof(DrawCommand));
    }

    vkCmdEndRendering(cmd.get());

    m_query.end(cmd);
}


VkPipeline tmp_create_graphics_pipeline(gpu_t &gpu, VkPipelineLayout layout, const Shader &shader) {

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
