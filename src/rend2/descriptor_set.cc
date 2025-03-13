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

void DescriptorSet::write_combined_image_sampler(u32 binding, u32 array_index, VkImage image, VkImageView view, VkSampler sampler) {
    m_writer.write_combined_image_sampler(binding, array_index, view, sampler);

    m_dependency.add_image(
        VK_PIPELINE_STAGE_2_HOST_BIT_KHR,
        VK_ACCESS_2_HOST_WRITE_BIT_KHR,
        image,
        VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        VK_IMAGE_LAYOUT_UNDEFINED);
}
void DescriptorSet::write_uniform_buffer(u32 binding, u32 array_index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
    m_writer.write_uniform_buffer(binding, array_index, buffer, offset, range);
}
void DescriptorSet::write_storage_buffer(u32 binding, u32 array_index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
    m_writer.write_storage_buffer(binding, array_index, buffer, offset, range);
}

Dependency DescriptorSet::flush(gpu_t &gpu) {
    m_writer.update_set(gpu, m_set);
    m_writer.clear();

    auto result_deps = m_dependency;
    m_dependency.clear();
    return result_deps;
}
