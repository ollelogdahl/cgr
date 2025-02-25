#include "pipeline_layout_builder.h"

PipelineLayoutBuilder &PipelineLayoutBuilder::add_descriptor_set(VkDescriptorSetLayout descriptor_set) {
    descriptor_sets.push_back(descriptor_set);

    return *this;
}

PipelineLayoutBuilder &PipelineLayoutBuilder::add_push_constant_range(const VkPushConstantRange &range) {
    push_constants.push_back(range);

    return *this;
}
VkPipelineLayout PipelineLayoutBuilder::build(gpu_t &gpu) {

    VkPipelineLayoutCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create_info.pNext = nullptr;
    create_info.flags = 0;
    create_info.setLayoutCount = descriptor_sets.size();
    create_info.pSetLayouts = descriptor_sets.data();
    create_info.pushConstantRangeCount = push_constants.size();
    create_info.pPushConstantRanges = push_constants.data();

    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(gpu.device, &create_info, nullptr, &layout));

    return layout;
}