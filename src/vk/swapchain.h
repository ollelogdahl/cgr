#pragma once

#include "oc.h"
#include <vulkan/vulkan.h>

struct swapchain_t {
    VkSwapchainKHR handle;
    VkFormat image_format;
    VkExtent2D extent;

    u32 image_count;
    VkImage *images;
    VkImageView *image_views;
};

// i'm not the hugest fan of builder pattern, but it looks nice.
class swapchain_builder_t {
public:
    swapchain_builder_t(VkPhysicalDevice pdev, VkDevice device, VkSurfaceKHR surface,
        u32 graphics_queue_index, u32 present_queue_index);

    swapchain_builder_t &set_desired_format(VkSurfaceFormatKHR surface);
    swapchain_builder_t &set_desired_present_mode(VkPresentModeKHR mode);
    swapchain_builder_t &set_desired_extent(u32 width, u32 height);
    swapchain_builder_t &add_image_usage(VkImageUsageFlags usage);
    swapchain_builder_t &set_old_swapchain(swapchain_t &old_swapchain);

    result_t<swapchain_t, std::string> build();
private:
    VkPhysicalDevice pdev;
    VkDevice device;
    VkSurfaceKHR surface;
    VkSurfaceFormatKHR desired_format;
    VkPresentModeKHR desired_present_mode;
    VkExtent2D desired_extent;
    VkSwapchainKHR old_swapchain;

    VkImageUsageFlags image_usage;

    u32 graphics_queue_index;
    u32 present_queue_index;
};
