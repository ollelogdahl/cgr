#pragma once

#include "oc.h"
#include "gpu.h"

#include <span>
#include <vulkan/vulkan_core.h>
#include <vma/vk_mem_alloc.h>

// this is very specific. But we will try it out!
struct WriteDependency {
    WriteDependency() = default;

    void join(WriteDependency &other) {
        barriers.insert(barriers.end(), other.barriers.begin(), other.barriers.end());
    }

    std::vector<VkBufferMemoryBarrier2> barriers = {};

    void pipeline_barrier(VkCommandBuffer cmd, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.bufferMemoryBarrierCount = barriers.size();
        dependency.pBufferMemoryBarriers = barriers.data();

        for (auto &barrier : barriers) {
            barrier.dstStageMask = dst_stage;
            barrier.dstAccessMask = dst_access;
        }

        vkCmdPipelineBarrier2(cmd, &dependency);
    }
};

// this is a buffer which resides on the GPU.
// We should be able to provide a simplified usage parameter.
class GpuBuffer {
public:
    struct Write {
        u32 offset;
        std::span<byte> data;
    };
    typedef std::span<Write> WriteList;

    GpuBuffer() = default;
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
    WriteDependency write_with_barrier(VkCommandBuffer cmd, slice<byte> data) {
        return write_with_barrier(cmd, data, 0);
    }
    WriteDependency write_with_barrier(VkCommandBuffer cmd, slice<byte> data, u32 offset);

    WriteDependency multiwrite_with_barrier(VkCommandBuffer cmd, WriteList);

    void copy_to(VkCommandBuffer cmd, GpuBuffer &dst);

    VkBuffer get() const { return m_buffer; }
private:
    void ensure_staging_buffer_size(u32 size);

    gpu_t *m_gpu = nullptr;
    VkBuffer m_buffer = VK_NULL_HANDLE;
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
