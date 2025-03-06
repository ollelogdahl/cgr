#pragma once

#include <vulkan/vulkan_core.h>

struct RenderTarget {
    VkImageView color_view;
    VkImageView depth_view;
    VkExtent2D extent;
    bool clear_first;

    VkRenderingAttachmentInfo as_color_attachment() const;
    VkRenderingAttachmentInfo as_depth_attachment() const;
    VkViewport viewport() const;
    VkRect2D scissor() const;
};
