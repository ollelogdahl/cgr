#include "vku.h"

VkFence create_fence(gpu_t &gpu, bool signal) {
    VkFence fence;
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = signal ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
    VK_CHECK(vkCreateFence(gpu.device, &fence_info, nullptr, &fence));
    return fence;
}

void set_object_name(gpu_t &gpu, VkObjectType type, void *handle, std::string name) {
    /*
    // allocate the name like crazy!
    char *name_copy = new char[name.size() + 1];
    memcpy(name_copy, name.c_str(), name.size());

    VkDebugUtilsObjectNameInfoEXT name_info = {VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    name_info.objectType = type;
    name_info.objectHandle = (u64)handle;
    name_info.pObjectName = name_copy;
    vkSetDebugUtilsObjectNameEXT(gpu.device, &name_info);
     */
}
