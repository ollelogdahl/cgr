#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "oc.h"
#include "vma/vk_mem_alloc.h"

#include <vector>
#include <vulkan/vulkan_core.h>

#define VK_CHECK(...) do { VkResult result = __VA_ARGS__; if (result != VK_SUCCESS) { \
    auto err_str = vk_result_to_cstr(result); \
    panic("vulkan api error: {}", err_str); } } while(0)
const char * vk_result_to_cstr(VkResult result);

struct gpu_t {
    VkInstance instance;
    VkPhysicalDevice pdev = VK_NULL_HANDLE;
    VkDevice device;
    VkQueue graphics_queue, present_queue;
    VkSurfaceKHR surface;

    GLFWwindow *window;

    VkRenderPass display_render_pass;

    VkDebugUtilsMessengerEXT debug_messager;

    VmaAllocator allocator;

    struct {
        u64 timestamp_period;
    } limits;

    struct {
        bool timestamp_queries;
    } support;

    struct {
        u32 graphics;
        u32 present;
    } queue_families;

    // can maybe be broken out but i don't see why.
    struct {
        VkSwapchainKHR handle;
        std::vector<VkImage> images;
        VkFormat image_format;
        VkExtent2D extent;

        std::vector<VkImageView> image_views;
        std::vector<VkFramebuffer> framebuffers;
    } swapchain;

    void init(GLFWwindow *window);
    void recreate_swapchain(bool need_to_clear = true);

    // creates a 'persistent' buffer (optimized for gpu-only use) using a staging buffer.
    // to write to
    void create_buffer_persistent(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer &buffer, VmaAllocation &allocation);
};
