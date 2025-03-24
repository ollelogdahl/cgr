#include "descriptor_set.h"

void DescriptorSet::init(gpu_t &gpu, VkDescriptorPool pool, VkDescriptorSetLayout layout){
    m_layout = layout;

    // @todo: remove this! It breaks variable binding stuffies.
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout;

    VK_CHECK(vkAllocateDescriptorSets(gpu.device, &alloc_info, &m_set));
}

void DescriptorSet::write_combined_image_sampler(u32 binding, u32 array_index, VkImageView view, VkSampler sampler) {
    m_writer.write_combined_image_sampler(binding, array_index, view, sampler);
}
void DescriptorSet::write_uniform_buffer(u32 binding, u32 array_index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
    m_writer.write_uniform_buffer(binding, array_index, buffer, offset, range);
}
void DescriptorSet::write_storage_buffer(u32 binding, u32 array_index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
    m_writer.write_storage_buffer(binding, array_index, buffer, offset, range);
}
void DescriptorSet::write_acceleration_structure(u32 binding, u32 array_index, VkAccelerationStructureKHR acceleration_structure) {
    m_writer.write_acceleration_structure(binding, array_index, acceleration_structure);
}

void DescriptorSet::flush(gpu_t &gpu) {
    m_writer.update_set(gpu, m_set);
    m_writer.clear();
}
