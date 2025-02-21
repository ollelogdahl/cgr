#pragma once

#include "gpu.h"

class DescriptorSet {
public:
    void init(gpu_t &gpu, VkDescriptorPool pool, VkDescriptorSetLayout layout);

    void write_combined_image_sampler(u32 binding, u32 array_index, VkImageView view, VkSampler sampler);
    void write_buffer(u32 binding, u32 array_index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);

    void flush(gpu_t &gpu);

    VkDescriptorSet get() const { return m_set; }
    VkDescriptorSetLayout layout() const { return m_layout; }
private:
    VkDescriptorSet m_set;
    VkDescriptorSetLayout m_layout;
    descriptor_writer_t m_writer;
};
