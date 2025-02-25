#pragma once

#include "oc.h"
#include "gpu.h"
#include <vulkan/vulkan.h>

class DescriptorSetLayoutBuilder {
public:
    DescriptorSetLayoutBuilder &add_binding(u32 binding, VkDescriptorType type, VkShaderStageFlags stages);
    DescriptorSetLayoutBuilder &add_variable_binding(u32 binding, VkDescriptorType type, VkShaderStageFlags stages, u32 max_count);

    VkDescriptorSetLayout build(gpu_t &gpu);
private:
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VkDescriptorBindingFlags> binding_flags;
};