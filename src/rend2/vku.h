#pragma once

// vulkan utils

#include "gpu.h"
#include "rend2/command_buffer.h"
#include <vulkan/vulkan_core.h>

VkFence create_fence(gpu_t &gpu, bool signal = true);
VkSemaphore create_semaphore(gpu_t &gpu);

void set_object_name(gpu_t &gpu, VkObjectType type, void *handle, std::string name);

struct _DebugMarkScoped {
    _DebugMarkScoped(CommandBuffer &cmd, const char *name) : cmd(cmd) {
        VkDebugMarkerMarkerInfoEXT info = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT,
            .pMarkerName = name,
        };
        vkCmdDebugMarkerBeginEXT(cmd.get(), &info);

    }
    ~_DebugMarkScoped() {
        vkCmdDebugMarkerEndEXT(cmd.get());
    }
private:
    CommandBuffer &cmd;
};
