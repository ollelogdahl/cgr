#pragma once

#include "oc.h"
#include <fmt/format.h>
#include <vulkan/vulkan.h>

template <>
struct fmt::formatter<VkFormat> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const VkFormat& format, auto& ctx) const {
        switch(format) {
        case VK_FORMAT_UNDEFINED:
            return format_to(ctx.out(), "VK_FORMAT_UNDEFINED");
        default:
            return format_to(ctx.out(), "unknown format: {}", static_cast<u32>(format));
        }
    }
};

// this is really stupid. we just want to remove the need for sType, and
// make the code a bit more readable.
namespace vks {

constexpr VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info(
        slice<const VkDescriptorSetLayoutBinding> bindings) {
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = bindings.len;
    info.pBindings = bindings.data;
    return info;
}

constexpr VkPipelineLayoutCreateInfo pipeline_layout_create_info(
        slice<const VkDescriptorSetLayout> set_layouts,
        slice<const VkPushConstantRange> push_constant_ranges) {
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = set_layouts.len;
    info.pSetLayouts = set_layouts.data;
    info.pushConstantRangeCount = push_constant_ranges.len;
    info.pPushConstantRanges = push_constant_ranges.data;
    return info;
}

constexpr VkSemaphoreCreateInfo semaphore_create_info() {
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    return info;
}

constexpr VkFenceCreateInfo fence_create_info(VkFenceCreateFlags flags) {
    VkFenceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.flags = flags;
    return info;
}

constexpr VkCommandBufferBeginInfo command_buffer_begin_info(VkCommandBufferUsageFlags flags) {
    VkCommandBufferBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = flags;
    info.pInheritanceInfo = nullptr;
    return info;
}

constexpr VkRenderPassBeginInfo render_pass_begin_info(VkRenderPass render_pass,
        VkFramebuffer framebuffer,
        VkOffset2D offset,
        VkExtent2D extent,
        slice<const VkClearValue> clear_values) {
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = render_pass;
    info.framebuffer = framebuffer;
    info.renderArea.offset = offset;
    info.renderArea.extent = extent;
    info.clearValueCount = clear_values.len;
    info.pClearValues = clear_values.data;
    return info;
}

constexpr VkSubmitInfo submit_info(slice<const VkCommandBuffer> command_buffers,
        slice<const VkSemaphore> wait_semaphores,
        slice<const VkPipelineStageFlags> wait_stages,
        slice<const VkSemaphore> signals) {
    assert(wait_semaphores.len == wait_stages.len);

    VkSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    info.waitSemaphoreCount = wait_semaphores.len;
    info.pWaitSemaphores = wait_semaphores.data;
    info.pWaitDstStageMask = wait_stages.data;
    info.commandBufferCount = command_buffers.len;
    info.pCommandBuffers = command_buffers.data;
    info.signalSemaphoreCount = signals.len;
    info.pSignalSemaphores = signals.data;
    return info;
}

constexpr VkPresentInfoKHR present_info(slice<const VkSemaphore> wait_semaphores,
        slice<const VkSwapchainKHR> swapchains,
        slice<const u32> image_indices) {
    assert(wait_semaphores.len == swapchains.len);
    assert(wait_semaphores.len == image_indices.len);

    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = wait_semaphores.len;
    info.pWaitSemaphores = wait_semaphores.data;
    info.swapchainCount = swapchains.len;
    info.pSwapchains = swapchains.data;
    info.pImageIndices = image_indices.data;
    info.pResults = nullptr;

    return info;

}

}
