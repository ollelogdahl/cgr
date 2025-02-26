#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <functional>
#include <array>

#include <tracy/TracyVulkan.hpp>

class Gpu {
public:
    struct QueueFamilyIndices {
        uint32_t graphics;
        uint32_t compute;
        uint32_t transfer;
        uint32_t present;

        bool has_dedicated_compute() const { return compute != graphics; }
        bool has_dedicated_transfer() const { return transfer != graphics; }
    };

    class CommandStream {
    public:
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        TracyVkCtx tracy_ctx;

        std::vector<VkSemaphore> wait_semaphores;
        std::vector<VkPipelineStageFlags2> wait_stages;
        std::vector<VkSemaphore> signal_semaphores;
        VkFence completion_fence = VK_NULL_HANDLE;

        void reset();
        void begin();
        void end();
        void submit(VkQueue queue);

        // Barrier helpers
        void barrier(
            VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
            VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);

        void buffer_barrier(
            VkBuffer buffer,
            VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
            VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);

        void image_barrier(
            VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
            VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
            VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);
    };

    struct Frame {
        std::vector<CommandStream> compute_streams;
        CommandStream graphics_stream;

        VkSemaphore image_available;
        VkSemaphore render_finished;
        uint32_t image_idx;
    };

    struct CreateInfo {
        bool enable_validation = false;
        uint32_t max_frames_in_flight = 2;
    };

    Gpu(const CreateInfo& info);
    ~Gpu();

    // Disable copying
    Gpu(const Gpu&) = delete;
    Gpu& operator=(const Gpu&) = delete;

    void begin_frame();
    void end_frame();
    void wait_idle();

    // Frame execution
    void execute_frame(std::function<void(Gpu &, Frame&)> fn);

    // Command stream management
    CommandStream& add_compute_stream();

    // Resource creation helpers
    VkSemaphore create_semaphore();
    VkFence create_fence(bool signaled = false);

    // Getters
    VkDevice device() const { return m_device; }
    VkPhysicalDevice physical_device() const { return m_physical_device; }
    const QueueFamilyIndices& queue_families() const { return m_queue_families; }
    uint32_t current_frame_index() const { return current_frame; }

    VkQueue graphics_queue() const { return m_graphics_queue; }
    VkQueue compute_queue() const { return m_compute_queue; }
    VkQueue transfer_queue() const { return m_transfer_queue; }
    VkQueue present_queue() const { return m_present_queue; }

private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;

    QueueFamilyIndices m_queue_families;

    VkQueue m_graphics_queue = VK_NULL_HANDLE;
    VkQueue m_compute_queue = VK_NULL_HANDLE;
    VkQueue m_transfer_queue = VK_NULL_HANDLE;
    VkQueue m_present_queue = VK_NULL_HANDLE;

    VkCommandPool m_graphics_pool = VK_NULL_HANDLE;
    VkCommandPool m_compute_pool = VK_NULL_HANDLE;
    VkCommandPool m_transfer_pool = VK_NULL_HANDLE;

    uint32_t max_frames_ = 2;
    std::vector<Frame> frames;
    uint32_t current_frame = 0;

    void create_command_pools();
    void destroy_command_pools();
    void create_synchronization_objects();
    void destroy_synchronization_objects();
};
