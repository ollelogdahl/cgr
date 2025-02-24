#include "buffer.h"
#include "gpu.h"
#include <vulkan/vulkan_core.h>

// @note: optimization.
//
// We can usually infer on compile time that a buffer will not be used mapped.
// We essentially delay the runtime to check if the buffer is mapped or not.
// This is not a big deal, but it could be nicer for the reader to separate the
// `DynamicBuffer` and `StaticBuffer` into two different classes.

VkAccessFlags2 access_flags_from_usage(VkBufferUsageFlags usage);
VkPipelineStageFlags2 pipeline_stage_from_usage(VkBufferUsageFlags usage);

GpuBuffer::GpuBuffer(gpu_t &gpu, u32 size, VkBufferUsageFlags usage)
: m_gpu(&gpu), m_mapped(nullptr), m_usage(usage) {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VK_CHECK(vmaCreateBuffer(gpu.allocator, &buffer_info, &alloc_info, &m_buffer, &m_allocation, &m_allocation_info));

    VkMemoryPropertyFlags mem_props;
    vmaGetAllocationMemoryProperties(gpu.allocator, m_allocation, &mem_props);

    if (mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        m_mapped = (byte *)m_allocation_info.pMappedData;
    }
}

void GpuBuffer::write(slice<byte> data, u32 offset) {
    if (m_mapped) {
        memcpy(m_mapped, data.data, data.len);
        VK_CHECK(vmaFlushAllocation(m_gpu->allocator, m_allocation, offset, data.len));
    } else {
        ensure_staging_buffer_size(data.len);

        memcpy(staging.allocation_info.pMappedData, data.data, data.len);
        VK_CHECK(vmaFlushAllocation(m_gpu->allocator, staging.allocation, 0, VK_WHOLE_SIZE));

        VkBufferMemoryBarrier staging_barrier = {};
        staging_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        staging_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        staging_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        staging_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        staging_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        staging_barrier.buffer = staging.buffer;
        staging_barrier.offset = 0;
        staging_barrier.size = VK_WHOLE_SIZE;

        auto cmd = m_gpu->begin_single_use_command_buffer();
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &staging_barrier, 0, nullptr);

        VkBufferCopy copy_region {
            .srcOffset = 0,
            .dstOffset = offset,
            .size = data.len
        };
        vkCmdCopyBuffer(cmd, staging.buffer, m_buffer, 1, &copy_region);
        m_gpu->end_single_use_command_buffer(cmd);
    }
}

void GpuBuffer::write_with_barrier(VkCommandBuffer cmd, slice<byte> data, u32 offset) {
    if (m_mapped) {
        memcpy(m_mapped + offset, data.data, data.len);
        VK_CHECK(vmaFlushAllocation(m_gpu->allocator, m_allocation, offset, data.len));

        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_HOST_BIT;
        barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstStageMask = pipeline_stage_from_usage(m_usage);
        barrier.dstAccessMask = access_flags_from_usage(m_usage);
        barrier.buffer = m_buffer;
        barrier.offset = offset;
        barrier.size = data.len;

        VkDependencyInfo dependency_info{};
        dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency_info.bufferMemoryBarrierCount = 1;
        dependency_info.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency_info);
    } else {
        ensure_staging_buffer_size(data.len);

        memcpy(staging.allocation_info.pMappedData, data.data, data.len);
        VK_CHECK(vmaFlushAllocation(m_gpu->allocator, staging.allocation, 0, VK_WHOLE_SIZE));

        VkBufferMemoryBarrier staging_barrier = {};
        staging_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        staging_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        staging_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        staging_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        staging_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        staging_barrier.buffer = staging.buffer;
        staging_barrier.offset = 0;
        staging_barrier.size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &staging_barrier, 0, nullptr);

        VkBufferCopy copy_region {
            .srcOffset = 0,
            .dstOffset = offset,
            .size = data.len
        };
        vkCmdCopyBuffer(cmd, staging.buffer, m_buffer, 1, &copy_region);

        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = pipeline_stage_from_usage(m_usage);
        barrier.dstAccessMask = access_flags_from_usage(m_usage);
        barrier.buffer = m_buffer;
        barrier.offset = offset;
        barrier.size = data.len;

        VkDependencyInfo dependency_info{};
        dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency_info.bufferMemoryBarrierCount = 1;
        dependency_info.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency_info);
    }
}

void GpuBuffer::multiwrite_with_barrier(VkCommandBuffer cmd, WriteList wl) {
    // writing multiple slices to the buffer but with only a single barrier.
    if (m_mapped) {
        for (usize i = 0; i < wl.size(); ++i) {
            memcpy(m_mapped + wl[i].offset, wl[i].data.data(), wl[i].data.size());
        }
        VK_CHECK(vmaFlushAllocation(m_gpu->allocator, m_allocation, 0, VK_WHOLE_SIZE));

        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_HOST_BIT;
        barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstStageMask = pipeline_stage_from_usage(m_usage);
        barrier.dstAccessMask = access_flags_from_usage(m_usage);
        barrier.buffer = m_buffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;

        VkDependencyInfo dependency_info{};
        dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency_info.bufferMemoryBarrierCount = 1;
        dependency_info.pBufferMemoryBarriers = &barrier;
        // @todo: defer barrier!vkCmdPipelineBarrier2(cmd, &dependency_info);
    } else {
        auto staging_offsets = std::vector<u32>(wl.size());
        auto req_staging_size = 0;
        for (usize i = 0; i < wl.size(); ++i) {
            staging_offsets[i] = req_staging_size;
            req_staging_size += wl[i].data.size();
        }

        ensure_staging_buffer_size(req_staging_size);

        for (usize i = 0; i < wl.size(); ++i) {
            memcpy((byte *)staging.allocation_info.pMappedData + wl[i].offset, wl[i].data.data(), wl[i].data.size());
        }
        VK_CHECK(vmaFlushAllocation(m_gpu->allocator, staging.allocation, 0, VK_WHOLE_SIZE));

        VkBufferMemoryBarrier staging_barrier = {};
        staging_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        staging_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        staging_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        staging_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        staging_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        staging_barrier.buffer = staging.buffer;
        staging_barrier.offset = 0;
        staging_barrier.size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &staging_barrier, 0, nullptr);

        auto copy_regions = std::vector<VkBufferCopy>(wl.size());
        for (usize i = 0; i < wl.size(); ++i) {
            copy_regions[i] = {
                .srcOffset = staging_offsets[i],
                .dstOffset = wl[i].offset,
                .size = wl[i].data.size()
            };
        }

        vkCmdCopyBuffer(cmd, staging.buffer, m_buffer, copy_regions.size(), copy_regions.data());

        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = pipeline_stage_from_usage(m_usage);
        barrier.dstAccessMask = access_flags_from_usage(m_usage);
        barrier.buffer = m_buffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;

        VkDependencyInfo dependency_info{};
        dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency_info.bufferMemoryBarrierCount = 1;
        dependency_info.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency_info);
    }
}


void GpuBuffer::ensure_staging_buffer_size(u32 size) {
    if (staging.buffer != VK_NULL_HANDLE && staging.allocation_info.size >= size) {
        return;
    }

    if (staging.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_gpu->allocator, staging.buffer, staging.allocation);
    }

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VK_CHECK(vmaCreateBuffer(m_gpu->allocator, &buffer_info, &alloc_info, &staging.buffer, &staging.allocation, &staging.allocation_info));
}

VkAccessFlags2 access_flags_from_usage(VkBufferUsageFlags usage) {
    VkAccessFlags2 flags = 0;
    if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
        flags |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    }
    if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
        flags |= VK_ACCESS_2_INDEX_READ_BIT;
    }
    if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
        flags |= VK_ACCESS_2_UNIFORM_READ_BIT;
    }
    if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
        flags |= VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    }
    return flags;
}

VkPipelineStageFlags2 pipeline_stage_from_usage(VkBufferUsageFlags usage) {
    VkPipelineStageFlags2 flags = 0;
    if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
        flags |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    }
    if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
        flags |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    }
    if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
        flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    }
    return flags;
}
