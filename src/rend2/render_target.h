#pragma once

#include <volk/volk.h>

struct RenderTarget {
    VkImageView color_view;
    VkImageView depth_view;
    VkOffset2D offset = {0, 0};
    VkExtent2D extent;
    bool clear_first;

    VkRenderingAttachmentInfo as_color_attachment() const;
    VkRenderingAttachmentInfo as_depth_attachment() const;
    VkViewport viewport() const;
    VkRect2D scissor() const;
};
