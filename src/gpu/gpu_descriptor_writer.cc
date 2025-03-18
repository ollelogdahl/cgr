#include "gpu_impl.h"

void descriptor_writer_t::clear() {
    image_infos.clear();
    buffer_infos.clear();
    writes.clear();
    acceleration_structure_infos.clear();
    acceleration_structures.clear();
}

void descriptor_writer_t::update_set(gpu_t &gpu, VkDescriptorSet set) {
    for (auto &write : writes) {
        write.dstSet = set;
    }

    vkUpdateDescriptorSets(gpu.device, writes.size(), writes.data(), 0, nullptr);
}


void descriptor_writer_t::write_combined_image_sampler(u32 binding, u32 array_index, VkImageView view, VkSampler sampler) {
    VkDescriptorImageInfo &image_info = image_infos.emplace_back(VkDescriptorImageInfo{
        .sampler = sampler,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    });

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = VK_NULL_HANDLE;
    write.dstBinding = binding;
    write.dstArrayElement = array_index;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &image_info;

    writes.push_back(write);
}

void descriptor_writer_t::write_uniform_buffer(u32 binding, u32 array_index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
    VkDescriptorBufferInfo &buffer_info = buffer_infos.emplace_back(VkDescriptorBufferInfo{
        .buffer = buffer,
        .offset = offset,
        .range = range
    });

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = VK_NULL_HANDLE;
    write.dstBinding = binding;
    write.dstArrayElement = array_index;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_info;

    writes.push_back(write);
}

void descriptor_writer_t::write_storage_buffer(u32 binding, u32 array_index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
    VkDescriptorBufferInfo &buffer_info = buffer_infos.emplace_back(VkDescriptorBufferInfo{
        .buffer = buffer,
        .offset = offset,
        .range = range
    });

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = VK_NULL_HANDLE;
    write.dstBinding = binding;
    write.dstArrayElement = array_index;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_info;

    writes.push_back(write);
}

void descriptor_writer_t::write_acceleration_structure(u32 binding, u32 array_index, VkAccelerationStructureKHR acceleration_structure) {

    auto &acc = acceleration_structures.emplace_back(acceleration_structure);

    VkWriteDescriptorSetAccelerationStructureKHR acceleration_structure_info = {};
    acceleration_structure_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    acceleration_structure_info.accelerationStructureCount = 1;
    acceleration_structure_info.pAccelerationStructures = &acc;

    auto &acc_info = acceleration_structure_infos.emplace_back(acceleration_structure_info);

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = &acc_info;
    write.dstSet = VK_NULL_HANDLE;
    write.dstBinding = binding;
    write.dstArrayElement = array_index;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    write.descriptorCount = 1;

    writes.push_back(write);
}
