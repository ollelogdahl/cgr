#include "gpu_impl.h"

void gpu_t::create_image(usize width, usize height, VkFormat format, VkImageUsageFlags usage, gpu_image_t &image) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // this should be configurable.
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_CHECK(vmaCreateImage(allocator, &imageInfo, &alloc_info, &image.image, &image.allocation, nullptr));

    image.owner = this;
}

void gpu_t::create_image(slice<u8> data, usize width, usize height, VkFormat format, VkImageUsageFlags usage, bool mipmap, gpu_image_t &image) {
    // @todo: make an api providing barriers and stuff.

    // we need to create a staging buffer for the image data.
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

        VK_CHECK(vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &staging_buffer, &staging_allocation, &staging_allocation_info));

        VK_CHECK(vmaCopyMemoryToAllocation(allocator, data.data, staging_allocation, 0, data.len));
    }

    create_image(width, height, format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | usage, image);

    auto cmd = begin_single_use_command_buffer();
    {
        transition_image(cmd, image.image, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copy_region = {};
		copy_region.bufferOffset = 0;
		copy_region.bufferRowLength = 0;
		copy_region.bufferImageHeight = 0;

		copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.imageSubresource.mipLevel = 0;
		copy_region.imageSubresource.baseArrayLayer = 0;
		copy_region.imageSubresource.layerCount = 1;
		copy_region.imageExtent = { .width = (u32)width, .height = (u32)height, .depth = 1 };

		vkCmdCopyBufferToImage(cmd, staging_buffer, image.image,
		  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

		// @todo: this is kinda hard-coded (VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		transition_image(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    end_single_use_command_buffer(cmd);

    vmaDestroyBuffer(allocator, staging_buffer, staging_allocation);

    image.owner = this;
}

gpu_image_t::~gpu_image_t() {
    // if (image == VK_NULL_HANDLE) return;
    // vmaDestroyImage(owner->allocator, image, allocation);
}
