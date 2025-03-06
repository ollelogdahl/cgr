#include "descriptor_set_layout_builder.h"

DescriptorSetLayoutBuilder &DescriptorSetLayoutBuilder::add_binding(u32 binding, VkDescriptorType type, VkShaderStageFlags stages) {
    bindings.push_back(VkDescriptorSetLayoutBinding{
        .binding = binding,
        .descriptorType = type,
        .descriptorCount = 1,
        .stageFlags = stages
    });
    binding_flags.push_back(0);

    return *this;
}
DescriptorSetLayoutBuilder &DescriptorSetLayoutBuilder::add_variable_binding(u32 binding, VkDescriptorType type, VkShaderStageFlags stages, u32 max_count) {
    bindings.push_back(VkDescriptorSetLayoutBinding{
        .binding = binding,
        .descriptorType = type,
        .descriptorCount = max_count,
        .stageFlags = stages
    });
    binding_flags.push_back(
        0 //VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
    );
    return *this;
}

VkDescriptorSetLayout DescriptorSetLayoutBuilder::build(gpu_t &gpu) {
    assert(bindings.size() == binding_flags.size());
    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_create_info = {};
    binding_flags_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    binding_flags_create_info.bindingCount = bindings.size();
    binding_flags_create_info.pBindingFlags = binding_flags.data();

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.pNext = &binding_flags_create_info;
    layout_info.bindingCount = bindings.size();
    layout_info.pBindings = bindings.data();

    VkDescriptorSetLayout layout;
    VK_CHECK(vkCreateDescriptorSetLayout(gpu.device, &layout_info, nullptr, &layout));

    fmt::println("created descriptor set layout {}", (void *)layout);
    for (auto &binding : bindings) {
        fmt::println("  binding {} type {} count {} stages {}", binding.binding, (u32)binding.descriptorType, binding.descriptorCount, (u32)binding.stageFlags);
    }

    return layout;
}
