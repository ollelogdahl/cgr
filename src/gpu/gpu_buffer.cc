#include "gpu_impl.h"

#include "vks.h"

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
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

    vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &buffer.handle, &buffer.allocation, nullptr);

    buffer.owner = this;
}

void gpu_t::write_buffer(gpu_buffer_t &buffer, slice<u8> data) {
    // @todo: handle if the buffer is not host visible.
    void *address;
    vmaMapMemory(allocator, buffer.allocation, &address);
    memcpy(address, data.data, data.len);
    vmaUnmapMemory(allocator, buffer.allocation);
}

gpu_buffer_t::~gpu_buffer_t() {
    if (handle == VK_NULL_HANDLE) return;
    vmaDestroyBuffer(owner->allocator, handle, allocation);
}
