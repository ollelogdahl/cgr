#pragma once

#include <deque>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "oc.h"
#include "vk/swapchain.h"
#include "vma/vk_mem_alloc.h"

#include <vector>
#include <functional>
#include <vulkan/vulkan_core.h>

#include <tracy/TracyVulkan.hpp>

#include "rend2/command_buffer.h"

#define VK_CHECK(...) do { VkResult result = __VA_ARGS__; if (result != VK_SUCCESS) { \
    auto err_str = vk_result_to_cstr(result); \
    panic("vulkan api error: {}", err_str); } } while(0)
const char * vk_result_to_cstr(VkResult result);

#define MAX_FRAMES_IN_FLIGHT 2

struct gpu_t;

#define DECL_KEY(name) \
    bool operator==(const name &lhs, const name &rhs); \
    template <> struct std::hash<name> { \
        std::size_t operator()(const name &params) const; \
    };

enum class gpu_cull_mode_t {
    none,
    front,
    back,
};

struct gpu_buffer_t {
    gpu_t *owner;
    VkBuffer handle = VK_NULL_HANDLE;
    VmaAllocation allocation;

    void *mapped = nullptr;

    ~gpu_buffer_t();
};
struct gpu_image_t {
    gpu_t *owner;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation;
    VkImageView view;

    ~gpu_image_t();
};

struct shader_program_load_params_t {
    // use either glsl or spv.
    std::string vertex_glsl_path;
    std::string fragment_glsl_path;
    std::string vertex_spv_path;
    std::string fragment_spv_path;
};
DECL_KEY(shader_program_load_params_t)

struct gpu_shader_t {
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    shader_program_load_params_t params;
    bool modified = false;
};

struct pipeline_layout_config_t {
    VkPipelineLayoutCreateFlags flags;
    std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
    std::vector<VkPushConstantRange> push_constant_ranges;
};
DECL_KEY(pipeline_layout_config_t)

struct pipeline_config_t {
    ref_t<gpu_shader_t> shader;
    gpu_cull_mode_t cull_mode = gpu_cull_mode_t::back;

    struct pipeline_layout_config_t layout;

    // @todo: this could have a nicer api.
    struct {
        std::vector<VkVertexInputBindingDescription> bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;
    } vertex_input_info;

    struct {
        bool depth_test;
        bool depth_write;
        VkCompareOp depth_compare_op;
    } depth_stencil;

    std::vector<VkFormat> color_attachment_formats;
    VkFormat depth_attachment_format;
};
DECL_KEY(pipeline_config_t)

struct gpu_pipeline_t {
    VkPipeline pipeline;
    VkPipelineLayout layout;
};

struct buffer_write_barrier_t {
    VkDependencyInfo dependency_info = {};
    std::vector<VkBufferMemoryBarrier2> barriers = {};

    void set_dst(VkPipelineStageFlags2KHR stage, VkAccessFlags2KHR access) {
        barriers.back().dstStageMask = stage;
        barriers.back().dstAccessMask = access;
    }
};

struct gpu_create_options_t {
    bool request_validation_layers = false;
};

struct gpu_t {
    VkInstance instance;
    VkPhysicalDevice pdev = VK_NULL_HANDLE;
    VkDevice device;
    VkQueue graphics_queue, present_queue, compute_queue;
    VkSurfaceKHR surface;

    GLFWwindow *window;

    VkDebugUtilsMessengerEXT debug_messager;

    VkCommandPool command_pool;
    VkCommandPool compute_command_pool;
    VkCommandPool transient_command_pool;
    VmaAllocator allocator;

    struct {
        u64 timestamp_period;
    } limits;

    struct {
        bool timestamp_queries;
        bool pipeline_statistics;
        bool depth_bias_clamp;
    } support;

    struct {
        u32 graphics;
        u32 present;
        u32 compute;
    } queue_families;

    swapchain_t swapchain;

    struct frame_t {
        VkSemaphore image_available;
        VkSemaphore render_finished;
        VkFence in_flight;

        // @todo: we should maybe support multiple compute command buffers?
        VkSemaphore compute_finished;
        VkFence compute_in_flight;
        VkFence compute_fence;

        CommandBuffer cmd;
        CommandBuffer compute_cmd;

        u32 image_idx;
    };

    gpu_image_t depth_image;

    frame_t frames[MAX_FRAMES_IN_FLIGHT];
    u64 frame_index = 0;

    void init(GLFWwindow *window, const gpu_create_options_t &options);
    void recreate_swapchain(u32 width, u32 height);

    ref_t<gpu_pipeline_t> make_pipeline(const pipeline_config_t &config);

    // to be called from the resource loader.
    void rebuild_pipelines();

    // @todo: make this extendable. We can probably provide a callback function
    // which in turn will run the block using the gpu as context.
    void frame(std::function<void(frame_t &)> fn);

    // creates a 'persistent' buffer (optimized for gpu-only use) using a staging buffer.
    // to write to
    void create_buffer_persistent(slice<u8> data, VkBufferUsageFlags usage, gpu_buffer_t &buffer);

    // creates a buffer which is memory mapped to the cpu. Really cool!
    void create_buffer(usize size, VkBufferUsageFlags usage, gpu_buffer_t &buffer);
    void write_buffer(gpu_buffer_t &buffer, slice<u8> data);

    void write_buffer_with_barrier(gpu_buffer_t &buffer, slice<u8> data,
        VkCommandBuffer cmd,
        buffer_write_barrier_t &barrier);

    void create_image(usize width, usize height, VkFormat format, VkImageUsageFlags usage, gpu_image_t &image);
    void create_image(slice<u8> data, usize width, usize height, VkFormat format, VkImageUsageFlags usage, bool mipmap, gpu_image_t &image);

    VkCommandBuffer begin_single_use_command_buffer();
    void end_single_use_command_buffer(VkCommandBuffer cmd);

private:
    std::unordered_map<pipeline_config_t, ref_t<gpu_pipeline_t>> loaded_pipelines;
    std::unordered_map<pipeline_layout_config_t, VkPipelineLayout> pipeline_layouts;
};

class descriptor_writer_t {
public:
    descriptor_writer_t() = default;
    void clear();
    void update_set(gpu_t &gpu, VkDescriptorSet set);

    void write_combined_image_sampler(u32 binding, u32 array_index, VkImageView view, VkSampler sampler);
    void write_uniform_buffer(u32 binding, u32 array_index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);
    void write_storage_buffer(u32 binding, u32 array_index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);
private:
    std::deque<VkDescriptorImageInfo> image_infos;
    std::deque<VkDescriptorBufferInfo> buffer_infos;
    std::vector<VkWriteDescriptorSet> writes;
};

void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);
