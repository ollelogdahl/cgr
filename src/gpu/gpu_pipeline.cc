#include "gpu.h"
#include "gpu_impl.h"

ref_t<gpu_pipeline_t> gpu_t::make_pipeline(const pipeline_config_t &config) {
    // making the pipeline does not neccessarily create it. It will be created
    // when
    auto exists_it = loaded_pipelines.find(config);
    if (exists_it != loaded_pipelines.end()) {
        return exists_it->second;
    }

    auto pipeline = make_ref<gpu_pipeline_t>();
    pipeline->pipeline = VK_NULL_HANDLE;

    {
        // create the pipeline layout.
        //
        // @todo: good api for pipeline creation where we can specify pushConstants
        // and descriptor sets.
        auto it = pipeline_layouts.find(config.layout);
        if (it != pipeline_layouts.end()) {
            pipeline->layout = it->second;
        } else {

            VkPipelineLayoutCreateInfo pipeline_layout_info{};
            pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipeline_layout_info.flags = config.layout.flags;
            pipeline_layout_info.setLayoutCount = config.layout.descriptor_set_layouts.size();
            pipeline_layout_info.pSetLayouts = config.layout.descriptor_set_layouts.data();
            pipeline_layout_info.pushConstantRangeCount = config.layout.push_constant_ranges.size();
            pipeline_layout_info.pPushConstantRanges = config.layout.push_constant_ranges.data();

            VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline->layout));
            pipeline_layouts[config.layout] = pipeline->layout;
        }
    }

    loaded_pipelines[config] = pipeline;
    gpu_log.info("pipeline :{:p} created", pipeline);

    return pipeline;
}

void gpu_t::rebuild_pipelines() {
    for (auto &[config, pipeline] : loaded_pipelines) {
        auto has_shader_changed = config.shader->modified;
        if (!has_shader_changed && pipeline->pipeline != VK_NULL_HANDLE) continue;

        gpu_log.info("rebuilding pipeline :{:p}", pipeline);

        if (pipeline->pipeline != VK_NULL_HANDLE) {
            // @todo: cleanup the old pipeline.
            pipeline->pipeline = VK_NULL_HANDLE;
        }

        // create the pipeline itself.
        {
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
            rendering_info.colorAttachmentCount = config.color_attachment_formats.size();
            rendering_info.pColorAttachmentFormats = config.color_attachment_formats.data();
            rendering_info.depthAttachmentFormat = config.depth_attachment_format;
            rendering_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

            // @todo: make this configurable:
            // it has to be here for now as we need to shuffle the pointers around.
            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_FALSE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

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

            pipeline_info.stageCount = config.shader->stages.size();
            pipeline_info.pStages = config.shader->stages.data();

            pipeline_info.pNext = &rendering_info;

            VkPipelineVertexInputStateCreateInfo vertex_input_info{};
            {
                vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertex_input_info.vertexBindingDescriptionCount = config.vertex_input_info.bindings.size();
                vertex_input_info.pVertexBindingDescriptions = config.vertex_input_info.bindings.data();
                vertex_input_info.vertexAttributeDescriptionCount = config.vertex_input_info.attributes.size();
                vertex_input_info.pVertexAttributeDescriptions = config.vertex_input_info.attributes.data();

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
                depth_stencil_state.depthTestEnable = config.depth_stencil.depth_test;
                depth_stencil_state.depthWriteEnable = config.depth_stencil.depth_write;
                depth_stencil_state.depthCompareOp = config.depth_stencil.depth_compare_op;
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
            pipeline_info.layout = pipeline->layout;
            pipeline_info.subpass = 0;
            pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

            if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline->pipeline) != VK_SUCCESS) {
                gpu_log.error("failed to create graphics pipeline");
            }

        }
    }
}

inline bool operator ==(const VkPushConstantRange &a, const VkPushConstantRange &b) {
    return a.stageFlags == b.stageFlags &&
           a.offset == b.offset &&
           a.size == b.size;
}

bool operator==(const pipeline_layout_config_t &a, const pipeline_layout_config_t &b) {
    if (a.flags != b.flags) return false;
    if (a.descriptor_set_layouts != b.descriptor_set_layouts) return false;
    if (a.push_constant_ranges != b.push_constant_ranges) return false;

    return true;
}

template <>
struct std::hash<VkPushConstantRange> {
    std::size_t operator()(const VkPushConstantRange &range) const;
};

template <>
struct std::hash<VkDescriptorSetLayout> {
    std::size_t operator()(const VkDescriptorSetLayout &layout) const;
};

std::size_t std::hash<VkPushConstantRange>::operator()(const VkPushConstantRange &range) const {
    usize h = 0;
    h ^= std::hash<VkShaderStageFlags>{}(range.stageFlags);
    h ^= std::hash<u32>{}(range.offset);
    h ^= std::hash<u32>{}(range.size);

    return h;
}

std::size_t std::hash<VkDescriptorSetLayout>::operator()(const VkDescriptorSetLayout &layout) const {
    return std::hash<u64>{}(reinterpret_cast<u64>(layout));
}

std::size_t std::hash<pipeline_layout_config_t>::operator()(const pipeline_layout_config_t &info) const {
    // @todo: can surely be done cheaper. We likely don't need to check everything..
    usize h = 0;
    h ^= std::hash<VkPipelineLayoutCreateFlags>{}(info.flags);
    // h ^= std::hash<std::vector<VkDescriptorSetLayout>>{}(info.descriptor_set_layouts);
    for (auto &layout : info.descriptor_set_layouts) {
        h ^= std::hash<VkDescriptorSetLayout>{}(layout);
    }
    for (auto &range : info.push_constant_ranges) {
        h ^= std::hash<VkPushConstantRange>{}(range);
    }

    return h;
}

bool operator==(const VkVertexInputBindingDescription &a, const VkVertexInputBindingDescription &b) {
    return a.binding == b.binding &&
           a.stride == b.stride &&
           a.inputRate == b.inputRate;
}

bool operator==(const VkVertexInputAttributeDescription &a, const VkVertexInputAttributeDescription &b) {
    return a.location == b.location &&
           a.binding == b.binding &&
           a.format == b.format &&
           a.offset == b.offset;
}

bool operator==(const pipeline_config_t &a, const pipeline_config_t &b) {
    if (a.shader != b.shader) return false;
    if (a.layout != b.layout) return false;
    if (a.vertex_input_info.bindings != b.vertex_input_info.bindings) return false;
    if (a.vertex_input_info.attributes != b.vertex_input_info.attributes) return false;
    if (a.color_attachment_formats != b.color_attachment_formats) return false;
    if (a.depth_attachment_format != b.depth_attachment_format) return false;

    return true;
}

std::size_t std::hash<pipeline_config_t>::operator()(const pipeline_config_t &config) const {
    // @todo: expand! but also, idgaf.
    std::size_t h = 0;
    h ^= std::hash<shader_program_load_params_t>{}(config.shader->params);
    h ^= std::hash<gpu_cull_mode_t>{}(config.cull_mode);
    h ^= std::hash<pipeline_layout_config_t>{}(config.layout);

    return h;
}
