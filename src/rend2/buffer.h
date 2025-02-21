#pragma once

#include "oc.h"
#include "gpu.h"

#include <vulkan/vulkan_core.h>
#include <vma/vk_mem_alloc.h>


// this is a buffer which resides on the GPU.
// We should be able to provide a simplified usage parameter.
class GpuBuffer {
public:
    struct Write {
        u32 offset;
        std::vector<byte> data;
    };
    typedef std::vector<Write> WriteList;

    GpuBuffer(gpu_t &gpu, u32 size, VkBufferUsageFlags usage);

    // immediate write to the entire buffer.
    // precond: data.len = size
    void write(slice<byte> data) {
        write(data, 0);
    }

    // immediate write to a specific offset in the buffer.
    // precond: data.len + offset <= size
    void write(slice<byte> data, u32 offset);

    template <typename T>
    void write(const T &data) {
        write(slice<byte>((byte *)&data, sizeof(T)), 0);
    }

    template <typename T>
    void write(const T &data, u32 offset) {
        write(slice<byte>((byte *)&data, sizeof(T)), offset);
    }

    // write to the buffer with a barrier.
    void write_with_barrier(VkCommandBuffer cmd, slice<byte> data) {
        write_with_barrier(cmd, data, 0);
    }
    void write_with_barrier(VkCommandBuffer cmd, slice<byte> data, u32 offset);

    void multiwrite_with_barrier(VkCommandBuffer cmd, WriteList &);

    void copy_to(VkCommandBuffer cmd, GpuBuffer &dst);

    VkBuffer get() const { return m_buffer; }
private:
    void ensure_staging_buffer_size(u32 size);

    gpu_t *m_gpu;
    VkBuffer m_buffer;
    VmaAllocation m_allocation;
    VmaAllocationInfo m_allocation_info;
    byte *m_mapped;
    VkBufferUsageFlags m_usage;

    struct {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation;
        VmaAllocationInfo allocation_info;
    } staging;
};
