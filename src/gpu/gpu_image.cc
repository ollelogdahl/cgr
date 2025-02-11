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

    gpu_log.info("create: image {}", (void *)image.image);
}

void gpu_t::create_image(slice<u8> data, usize width, usize height, VkFormat format, VkImageUsageFlags usage, bool mipmap, gpu_image_t &image) {
    // we need to create a staging buffer for the image data.
    gpu_buffer_t staging_buffer;
    create_buffer(data.len, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging_buffer);
    write_buffer(staging_buffer, data);

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

		vkCmdCopyBufferToImage(cmd, staging_buffer.handle, image.image,
		  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

		// @todo: this is kinda hard-coded (VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		transition_image(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    end_single_use_command_buffer(cmd);

    gpu_log.info("create: image {}", (void *)image.image);
}

gpu_image_t::~gpu_image_t() {
    if (image == VK_NULL_HANDLE) return;

    gpu_log.info("destroy: image {}", (void *)image);
    vmaDestroyImage(owner->allocator, image, allocation);
}
