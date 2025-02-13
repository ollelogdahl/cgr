#include "gpu.h"
#include "gpu_impl.h"

#include "vks.h"
#include <vulkan/vulkan_core.h>

void gpu_t::create_buffer_persistent(slice<u8> data, VkBufferUsageFlags usage, gpu_buffer_t &buffer) {

    VkBuffer staging_buffer;
    VmaAllocation staging_allocation;
    VmaAllocationInfo staging_allocation_info;

    {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = data.len;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        auto res = vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &staging_buffer, &staging_allocation, &staging_allocation_info);
        if (res != VK_SUCCESS) {
            gpu_log.error("failed to create staging buffer: {}", res);
            return;
        }
    }

    {
        // do copy
        auto res = vmaCopyMemoryToAllocation(allocator, data.data, staging_allocation, 0, data.len);
        if (res != VK_SUCCESS) {
            gpu_log.error("failed to copy data to staging buffer: {}", res);
            return;
        }
    }

    {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = data.len;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        auto res = vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &buffer.handle, &buffer.allocation, nullptr);
        if (res != VK_SUCCESS) {
            gpu_log.error("failed to create buffer: {}", res);
            return;
        }
    }

    {
        // perform move from staging to real
        VkCommandBuffer cmd = begin_single_use_command_buffer();

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = data.len;

        vkCmdCopyBuffer(cmd, staging_buffer, buffer.handle, 1, &copyRegion);

        end_single_use_command_buffer(cmd);
    }

    vmaDestroyBuffer(allocator, staging_buffer, staging_allocation);

    buffer.owner = this;
}

void gpu_t::create_buffer(usize size, VkBufferUsageFlags usage, gpu_buffer_t &buffer) {
    // im not sure we always want to do this. It works good though for UBOs i think.

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_create_info{};
    alloc_create_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_create_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT
        | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info;
    vmaCreateBuffer(allocator, &buffer_info, &alloc_create_info, &buffer.handle, &buffer.allocation, &alloc_info);

    VkMemoryPropertyFlags mem_props;
    vmaGetAllocationMemoryProperties(allocator, buffer.allocation, &mem_props);

    if (mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        buffer.mapped = alloc_info.pMappedData;
    } else {
        buffer.mapped = nullptr;
    }

    buffer.owner = this;
}

void gpu_t::write_buffer(gpu_buffer_t &buffer, slice<u8> data) {
    if (buffer.mapped) {
        memcpy(buffer.mapped, data.data, data.len);
    } else {
        panic("write_buffer for non-mapped buffer not implemented yet");
    }
}

void gpu_t::write_buffer_with_barrier(gpu_buffer_t &buffer, slice<u8> data, buffer_write_barrier_t &barrier_info) {
    // based on praxis:
    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html#usage_patterns_advanced_data_uploading

    if (buffer.mapped) {
        memcpy(buffer.mapped, data.data, data.len);
        vmaFlushAllocation(allocator, buffer.allocation, 0, VK_WHOLE_SIZE);

        // this removes other barriers from this object. well well.
        barrier_info.barriers.resize(1);
        auto &barrier = barrier_info.barriers[0];

        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_HOST_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
        // barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = buffer.handle;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;

        barrier_info.dependency_info = {};
        barrier_info.dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        barrier_info.dependency_info.bufferMemoryBarrierCount = 1;
        barrier_info.dependency_info.pBufferMemoryBarriers = barrier_info.barriers.data();
    } else {
        VkBuffer staging_buffer;
        VmaAllocation staging_allocation;
        VmaAllocationInfo staging_allocation_info;

        // Create staging buffer
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = data.len;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        // @todo: wah we should ot create buffer every time
        VK_CHECK(vmaCreateBuffer(allocator, &buffer_info, &alloc_info,
                                &staging_buffer, &staging_allocation, &staging_allocation_info));

        // Copy data to staging buffer
        VK_CHECK(vmaCopyMemoryToAllocation(allocator, data.data, staging_allocation, 0, data.len));

        // Set up barriers for the copy operation
        barrier_info.barriers.resize(2);

        // First barrier: Host write to transfer
        auto &barrier1 = barrier_info.barriers[0];
        barrier1 = {};
        barrier1.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier1.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        barrier1.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
        barrier1.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier1.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier1.buffer = staging_buffer;
        barrier1.offset = 0;
        barrier1.size = VK_WHOLE_SIZE;

        // Second barrier: Transfer to final usage
        auto &barrier2 = barrier_info.barriers[1];
        barrier2 = {};
        barrier2.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier2.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier2.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        // dstStageMask and dstAccessMask should be set by the caller
        barrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier2.buffer = buffer.handle;
        barrier2.offset = 0;
        barrier2.size = VK_WHOLE_SIZE;

        barrier_info.dependency_info = {};
        barrier_info.dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        barrier_info.dependency_info.bufferMemoryBarrierCount = 2;
        barrier_info.dependency_info.pBufferMemoryBarriers = barrier_info.barriers.data();

        // Perform the copy
        // @todo: should maybe be done in a separate command buffer
        VkCommandBuffer cmd = begin_single_use_command_buffer();

        VkBufferCopy copy_region{};
        copy_region.srcOffset = 0;
        copy_region.dstOffset = 0;
        copy_region.size = data.len;

        vkCmdCopyBuffer(cmd, staging_buffer, buffer.handle, 1, &copy_region);

        end_single_use_command_buffer(cmd);

        // Cleanup staging buffer
        vmaDestroyBuffer(allocator, staging_buffer, staging_allocation);
    }
}

gpu_buffer_t::~gpu_buffer_t() {
    if (handle == VK_NULL_HANDLE) return;

    if (mapped) {
        vmaUnmapMemory(owner->allocator, allocation);
    }

    vmaDestroyBuffer(owner->allocator, handle, allocation);
}
