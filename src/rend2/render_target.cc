#include "render_target.h"
#include "oc.h"

VkRenderingAttachmentInfo RenderTarget::as_color_attachment() const {
    return {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = color_view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = (clear_first) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}},
    };
}
VkRenderingAttachmentInfo RenderTarget::as_depth_attachment() const {
    return {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = (clear_first) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {{{1.0f, 0}}},
    };
}

VkViewport RenderTarget::viewport() const {
    return {
        .x = 0,
        .y = 0,
        .width = (f32)extent.width,
        .height = (f32)extent.height,
        .minDepth = 0,
        .maxDepth = 1,
    };
}
VkRect2D RenderTarget::scissor() const {
    return {
        .offset = {0, 0},
        .extent = extent,
    };
}
