#include "descriptor_set_layout_builder.h"

#include "gpu.h"

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
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
    );

    num_variable_descriptors += max_count;

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

void DescriptorSetLayoutBuilder::create_set(gpu_t &gpu, VkDescriptorPool pool, DescriptorSet &set) {

    VkDescriptorSetVariableDescriptorCountAllocateInfo variable_count_info = {};
    variable_count_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variable_count_info.descriptorSetCount = 1;
    variable_count_info.pDescriptorCounts = &num_variable_descriptors;

    VkDescriptorSetLayout layout = build(gpu);

    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.pNext = &variable_count_info;
    alloc_info.descriptorPool = pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout;

    VkDescriptorSet set_handle;
    VK_CHECK(vkAllocateDescriptorSets(gpu.device, &alloc_info, &set_handle));

    fmt::println("created descriptor set {}", (void *)set_handle);

    set.m_set = set_handle;
    set.m_layout = layout;
    set.m_writer = descriptor_writer_t();
}
