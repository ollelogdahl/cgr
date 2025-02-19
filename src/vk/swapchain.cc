#include "swapchain.h"

#include <vector>

struct swap_chain_support_details_t {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

swap_chain_support_details_t query_swap_chain_support(VkPhysicalDevice device, VkSurfaceKHR surface);

swapchain_builder_t::swapchain_builder_t(VkPhysicalDevice pdev, VkDevice device, VkSurfaceKHR surface,
    u32 graphics_queue_index, u32 present_queue_index) {
    this->pdev = pdev;
    this->device = device;
    this->surface = surface;
    this->graphics_queue_index = graphics_queue_index;
    this->present_queue_index = present_queue_index;

    this->old_swapchain = VK_NULL_HANDLE;
    this->image_usage = 0;
}

swapchain_builder_t &swapchain_builder_t::set_desired_format(VkSurfaceFormatKHR surface) {
    this->desired_format = surface;
    return *this;
}
swapchain_builder_t &swapchain_builder_t::set_desired_present_modes(const std::vector<VkPresentModeKHR> &modes) {
    this->desired_present_modes = modes;
    return *this;
}
swapchain_builder_t &swapchain_builder_t::set_desired_extent(u32 width, u32 height) {
    this->desired_extent = {width, height};
    return *this;
}
swapchain_builder_t &swapchain_builder_t::add_image_usage(VkImageUsageFlags usage) {
    this->image_usage |= usage;
    return *this;
}
swapchain_builder_t &swapchain_builder_t::set_old_swapchain(swapchain_t &old_swapchain) {
    this->old_swapchain = old_swapchain.handle;
    return *this;
}

result_t<swapchain_t, std::string> swapchain_builder_t::build() {
    swapchain_t swapchain;

    swap_chain_support_details_t swap_chain_support = query_swap_chain_support(pdev, surface);

    // ensure that desired format is available.
    {
        bool found = false;
        for (auto &fmt : swap_chain_support.formats) {
            if (fmt.format == desired_format.format && fmt.colorSpace == desired_format.colorSpace) {
                found = true;
                break;
            }
        }
        if (!found) {
            return std::string("desired format not available");
        }
    }

    // ensure that desired present mode is available.
    {
        bool found = false;
        VkPresentModeKHR found_mode;
        for (auto &desired : desired_present_modes) {
            if (found) break;

            for (auto &mode : swap_chain_support.present_modes) {
                if (mode == desired) {
                    found = true;
                    found_mode = mode;
                    break;
                }
            }    
        }
        if (!found) {
            return std::string("desired present mode not available");
        }
        swapchain.present_mode = found_mode;
    }

    // ensure that the desired extents are possible.
    {
        if (desired_extent.width < swap_chain_support.capabilities.minImageExtent.width) {
            return std::string("desired width too small");
        }
        if (desired_extent.height < swap_chain_support.capabilities.minImageExtent.height) {
            return std::string("desired height too small");
        }
        if (desired_extent.width > swap_chain_support.capabilities.maxImageExtent.width) {
            return std::string("desired width too large");
        }
        if (desired_extent.height > swap_chain_support.capabilities.maxImageExtent.height) {
            return std::string("desired height too large");
        }
    }

    u32 image_count = swap_chain_support.capabilities.minImageCount + 1;
    if (swap_chain_support.capabilities.maxImageCount > 0 && image_count > swap_chain_support.capabilities.maxImageCount) {
        image_count = swap_chain_support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = image_count;
    createInfo.imageFormat = desired_format.format;
    createInfo.imageColorSpace = desired_format.colorSpace;
    createInfo.imageExtent = desired_extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = image_usage;

    u32 qfamilies_separate[] = {graphics_queue_index, present_queue_index};
    if (graphics_queue_index != present_queue_index) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = qfamilies_separate;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0; // Optional
        createInfo.pQueueFamilyIndices = nullptr; // Optional
    }

    createInfo.preTransform = swap_chain_support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = swapchain.present_mode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = old_swapchain;

    auto result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain.handle);
    if (result != VK_SUCCESS) {
        return std::string("failed to create swap chain");
    }

    vkGetSwapchainImagesKHR(device, swapchain.handle, &image_count, nullptr);
    swapchain.image_count = image_count;
    swapchain.images = new VkImage[image_count];
    swapchain.image_views = new VkImageView[image_count];

    vkGetSwapchainImagesKHR(device, swapchain.handle, &image_count, swapchain.images);

    swapchain.image_format = desired_format.format;
    swapchain.extent = desired_extent;

    // create the image views
    // create image views into swap-chain.

    for (usize i = 0; i < image_count; i++) {
        auto &img = swapchain.images[i];

        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = img;
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchain.image_format;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        auto result = vkCreateImageView(device, &createInfo, nullptr, &swapchain.image_views[i]);
        if (result != VK_SUCCESS) {
            return std::string("failed to create image view");
        }
    }

    return swapchain;
}

swap_chain_support_details_t query_swap_chain_support(VkPhysicalDevice device, VkSurfaceKHR surface) {
    swap_chain_support_details_t details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);

    if (format_count != 0) {
        details.formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, details.formats.data());
    }

    uint32_t present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr);

    if (present_mode_count != 0) {
        details.present_modes.resize(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, details.present_modes.data());
    }

    return details;
}
