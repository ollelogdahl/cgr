#pragma once

#include "oc.h"
#include <fmt/format.h>
#include <vulkan/vulkan.h>
std::string format_as(VkFormat format);
std::string format_as(VkPresentModeKHR present_mode);
