#pragma once

#include "oc.h"
#include "gpu.h"
#include <volk/volk.h>

#include "descriptor_set.h"

class DescriptorSetLayoutBuilder {
public:
    DescriptorSetLayoutBuilder &add_binding(u32 binding, VkDescriptorType type, VkShaderStageFlags stages);
    DescriptorSetLayoutBuilder &add_late_binding(u32 binding, VkDescriptorType type, VkShaderStageFlags stages);
    DescriptorSetLayoutBuilder &add_variable_binding(u32 binding, VkDescriptorType type, VkShaderStageFlags stages, u32 max_count);

    VkDescriptorSetLayout build(gpu_t &gpu);
    void create_set(gpu_t &gpu, VkDescriptorPool pool, DescriptorSet &set);
private:
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VkDescriptorBindingFlags> binding_flags;
    u32 num_variable_descriptors = 0;

    bool any_late_bindings = false;
};
