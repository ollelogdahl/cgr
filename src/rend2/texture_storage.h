#pragma once

#include "gpu.h"
#include "rend2/render_handles.h"
#include "rend2/slotalloc.h"
#include <vulkan/vulkan.hpp>

// this is a holder of texture data on the GPU.
// It is intended to hold asset textures for rendering,
// as well as some intermediate textures.
//
// These are exposed to any shader using a single descriptor set.
//
// @todo: support transient textures.
// @todo: sparse texturing?
class TextureStorage {
public:
    TextureStorage(gpu_t &gpu, u32 max_textures);

    // no copy or move
    TextureStorage(const TextureStorage &) = delete;
    TextureStorage &operator=(const TextureStorage &) = delete;
    TextureStorage(TextureStorage &&) = delete;
    TextureStorage &operator=(TextureStorage &&) = delete;

    // flush descriptor updates. This needs to be done before using the descriptor set.
    void flush(vk::CommandBuffer &cmd);

    TextureHandle alloc_persistent();

    void update(TextureHandle handle, VkImageView view, VkSampler sampler);

    struct TextureInfo {
        VkImageView view;
        VkSampler sampler;
    };
    TextureInfo get(TextureHandle handle) const;

    VkDescriptorSet descriptor_set() const { return m_set; }
    VkDescriptorSetLayout descriptor_set_layout() const { return m_layout; }
private:
    gpu_t &m_gpu;

    u32 m_allocated;

    SlotAllocator m_texture_alloc;
    std::vector<TextureInfo> m_textures;

    vk::DescriptorPool m_descriptor_pool;

    vk::DescriptorSet m_set;
    vk::DescriptorSetLayout m_layout;


    descriptor_writer_t m_descriptor_writer;
};
