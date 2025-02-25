#pragma once

#include "gpu.h"

class PipelineLayoutBuilder {
public:
    PipelineLayoutBuilder &add_descriptor_set(VkDescriptorSetLayout descriptor_set);
    PipelineLayoutBuilder &add_push_constant_range(const VkPushConstantRange &range);

    VkPipelineLayout build(gpu_t &gpu);
private:
    std::vector<VkDescriptorSetLayout> descriptor_sets;
    std::vector<VkPushConstantRange> push_constants;
};