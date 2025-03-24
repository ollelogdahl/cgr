#include "texture_storage.h"
#include "rend2/vku.h"

TextureStorage::TextureStorage(gpu_t &gpu, u32 max_textures)
: m_gpu(gpu),
  m_texture_alloc(max_textures),
  m_textures(max_textures) {
    void *self_addr = this;
    vk::Device device = m_gpu.device;

    // create the pool
    {
        vk::DescriptorPoolSize pool_sizes[] = {
            { vk::DescriptorType::eCombinedImageSampler, max_textures },
        };

        vk::DescriptorPoolCreateInfo pool_info = {};
        pool_info.poolSizeCount = array_size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = 1;

        // do we want this? is this good?
        //pool_info.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;

        m_descriptor_pool = device.createDescriptorPool(pool_info);
        set_object_name(m_gpu, VK_OBJECT_TYPE_DESCRIPTOR_POOL, m_descriptor_pool,
            std::format("texture-storage-{} descriptor pool", self_addr));
    }

    // create the layout
    {
        vk::DescriptorSetLayoutBinding bindings[] = {
            vk::DescriptorSetLayoutBinding(
                0,
                vk::DescriptorType::eCombinedImageSampler,
                max_textures,
                vk::ShaderStageFlagBits::eFragment,
                nullptr
            )
        };
        vk::DescriptorBindingFlags binding_flags[] = {
            vk::DescriptorBindingFlagBits::ePartiallyBound
        };

        vk::DescriptorSetLayoutBindingFlagsCreateInfo binding_flags_create_info = {};
        binding_flags_create_info.bindingCount = array_size(bindings);
        binding_flags_create_info.pBindingFlags = binding_flags;

        vk::DescriptorSetLayoutCreateInfo layout_info = {};
        layout_info.bindingCount = array_size(bindings);
        layout_info.pBindings = bindings;
        layout_info.pNext = &binding_flags_create_info;

        m_layout = device.createDescriptorSetLayout(layout_info);
        set_object_name(m_gpu, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, m_layout,
            std::format("texture-storage-{} descriptor set layout", self_addr));
    }

    // create the descriptor set
    {
        vk::DescriptorSetAllocateInfo alloc_info = {};
        alloc_info.descriptorPool = m_descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &m_layout;

        m_set = device.allocateDescriptorSets(alloc_info)[0];
        set_object_name(m_gpu, VK_OBJECT_TYPE_DESCRIPTOR_SET, m_set,
            std::format("texture-storage-{} descriptor set", self_addr));
    }
}

void TextureStorage::flush(vk::CommandBuffer &cmd) {
    m_descriptor_writer.update_set(m_gpu, m_set);
}

TextureHandle TextureStorage::alloc_persistent() {
    u32 idx = m_texture_alloc.allocate();
    m_allocated += 1;
    return {idx};
}

void TextureStorage::update(TextureHandle handle, VkImageView view, VkSampler sampler) {
    m_descriptor_writer.write_combined_image_sampler(0, handle.id, view, sampler);

    m_textures[handle.id] = {view, sampler};
}
TextureStorage::TextureInfo TextureStorage::get(TextureHandle handle) const {
    return m_textures[handle.id];
}
