#pragma once

#include <fmt/format.h>
#include <vulkan/vulkan.h>

#define VK_CHECK(...) do { VkResult result = __VA_ARGS__; if (result != VK_SUCCESS) { \
    panic("vulkan api error: {}", result); } } while(0)

std::string format_as(VkResult result);
std::string format_as(VkFormat format);
std::string format_as(VkPresentModeKHR present_mode);
