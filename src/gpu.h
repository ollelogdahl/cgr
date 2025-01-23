#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "oc.h"
#include "vk/swapchain.h"
#include "vma/vk_mem_alloc.h"

#include <vector>
#include <functional>

#define VK_CHECK(...) do { VkResult result = __VA_ARGS__; if (result != VK_SUCCESS) { \
    auto err_str = vk_result_to_cstr(result); \
    panic("vulkan api error: {}", err_str); } } while(0)
const char * vk_result_to_cstr(VkResult result);

#define MAX_FRAMES_IN_FLIGHT 3

struct gpu_buffer_t {
    VkBuffer handle;
    VmaAllocation allocation;
};
struct gpu_image_t {
    VkImage image;
    VmaAllocation allocation;
    VkImageView view;
};

struct gpu_t {
    VkInstance instance;
    VkPhysicalDevice pdev = VK_NULL_HANDLE;
    VkDevice device;
    VkQueue graphics_queue, present_queue;
    VkSurfaceKHR surface;

    GLFWwindow *window;

    VkDebugUtilsMessengerEXT debug_messager;

    VkCommandPool command_pool;
    VkCommandPool transient_command_pool;
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

    swapchain_t swapchain;

    struct frame_t {
        VkSemaphore image_available;
        VkSemaphore render_finished;
        VkFence in_flight;

        // @todo: do we also need the pool here? idk?
        VkCommandBuffer cmds;

        // @todo: use a draw image instead.
        u32 image_idx;
    };

    gpu_image_t depth_image;

    frame_t frames[MAX_FRAMES_IN_FLIGHT];
    u32 frame_number = 0;

    void init(GLFWwindow *window);
    void recreate_swapchain(u32 width, u32 height);

    // @todo: make this extendable. We can probably provide a callback function
    // which in turn will run the block using the gpu as context.
    void frame(std::function<void(gpu_t &, frame_t &)> fn);

    // creates a 'persistent' buffer (optimized for gpu-only use) using a staging buffer.
    // to write to
    void create_buffer_persistent(slice<u8> data, VkBufferUsageFlags usage, gpu_buffer_t &buffer);

    // creates a buffer which is memory mapped to the cpu. Really cool!
    void create_buffer(usize size, VkBufferUsageFlags usage, gpu_buffer_t &buffer);
    void write_buffer(gpu_buffer_t &buffer, slice<u8> data);

    void create_image(usize width, usize height, VkFormat format, VkImageUsageFlags usage, gpu_image_t &image);

    VkCommandBuffer begin_single_use_command_buffer();
    void end_single_use_command_buffer(VkCommandBuffer cmd);
};
