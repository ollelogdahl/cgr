
#include "oc.h"
#include "vk/swapchain.h"
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>
class VulkanGlfwRenderContext {
public:
    struct CreateOptions {
        bool request_validation_layers = true;
        bool validation_syncronization = false;
        bool validation_best_practices = false;

        u32 frames_in_flight = 3;
    };

    VulkanGlfwRenderContext(GLFWwindow *window, const CreateOptions &options);
private:
    GLFWwindow *m_window;
    VkInstance m_instance;
    VkDebugUtilsMessengerEXT m_debug_messenger;

    VkSurfaceKHR m_surface;

    // all these are related to physical device.
    VkPhysicalDevice m_pdev;
    VkDevice m_device;
    struct {
        u32 graphics;
        u32 present;
        u32 compute;
    } m_queue_families;

    VkQueue m_graphics_queue;
    VkQueue m_present_queue;
    VkQueue m_compute_queue;

    struct {
        bool timestamp_queries;
        bool pipeline_statistics;

        u64 timestamp_period;

        u64 subgroup_size;
    } m_device_props;

    swapchain_t m_swapchain;

    struct Frame {
        VkSemaphore image_available;
        VkSemaphore render_finished;
        VkFence in_flight;

        u32 swapchain_image_idx;
    };
    std::vector<Frame> m_frames;
};
