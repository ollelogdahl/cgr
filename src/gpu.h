#pragma once

#include <deque>
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

struct gpu_t;

struct gpu_buffer_t {
    gpu_t *owner;
    VkBuffer handle = VK_NULL_HANDLE;
    VmaAllocation allocation;

    ~gpu_buffer_t();
};
struct gpu_image_t {
    gpu_t *owner;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation;
    VkImageView view;

    ~gpu_image_t();
};

// i am not convinced these types should live here. Although it is fine for now i guess.
// in reality, the resource loader should have it's own type 'managed_shader_program'
// which keeps both a gpu_shader_program but also the modified flag and the load params.
// I think that would be way nicer.
struct shader_program_load_params_t {
    const char *vertex_hlsl_path;
    const char *fragment_hlsl_path;

    bool operator==(const shader_program_load_params_t &other) const {
        return strcmp(vertex_hlsl_path, other.vertex_hlsl_path) == 0 &&
               strcmp(fragment_hlsl_path, other.fragment_hlsl_path) == 0;
    }
};
struct shader_program_t {
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    shader_program_load_params_t params;
    bool modified = false;
};

struct pipeline_layout_config_t {
    VkPipelineLayoutCreateFlags flags;
    slice<const VkDescriptorSetLayout> descriptor_set_layouts;
    slice<const VkPushConstantRange> push_constant_ranges;

    bool operator ==(const pipeline_layout_config_t &other) const {
        return flags == other.flags &&
               descriptor_set_layouts == other.descriptor_set_layouts &&
               push_constant_ranges == other.push_constant_ranges;
    }
};

inline bool operator ==(const VkPushConstantRange &a, const VkPushConstantRange &b) {
    return a.stageFlags == b.stageFlags &&
           a.offset == b.offset &&
           a.size == b.size;
}

struct pipeline_config_t {
    ref_t<shader_program_t> shader;

    struct pipeline_layout_config_t layout;

    // this could have a nicer api.
    struct {
        slice<const VkVertexInputBindingDescription> bindings;
        slice<const VkVertexInputAttributeDescription> attributes;
    } vertex_input_info;

    struct {
        bool depth_test;
        bool depth_write;
        VkCompareOp depth_compare_op;
    } depth_stencil;

    VkPipelineMultisampleStateCreateInfo multisampling;
    slice<const VkFormat> color_attachment_formats;
    VkFormat depth_attachment_format;

    bool operator ==(const pipeline_config_t &other) const {
        // @todo: fix this up when the config is done.
        return shader == other.shader;
    }
};

struct gpu_pipeline_t {
    VkPipeline pipeline;
    VkPipelineLayout layout;

    std::vector<VkFormat> color_attachment_formats;

    struct {
        std::vector<VkVertexInputBindingDescription> bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;
    } vertex_input_info;
};

template <>
struct std::hash<shader_program_load_params_t> {
    std::size_t operator()(const shader_program_load_params_t &params) const {
        std::size_t h1 = std::hash<const char*>{}(params.vertex_hlsl_path);
        std::size_t h2 = std::hash<const char*>{}(params.fragment_hlsl_path);
        return h1 ^ (h2 << 1);
    }
};

template <>
struct std::hash<pipeline_config_t> {
    std::size_t operator()(const pipeline_config_t &config) const {
        // @todo: hash all fields.
        std::size_t h1 = std::hash<shader_program_load_params_t>{}(config.shader->params);
        return h1;
    }
};

/*
u64 fnv1a(slice<u8> data) {
    u64 hash = 14695981039346656037u;
    for (u8 byte : data) {
        hash ^= byte;
        hash *= 1099511628211;
    }
    return hash;
}
 */

template <>
struct std::hash<pipeline_layout_config_t> {
    std::size_t operator()(const pipeline_layout_config_t &info) const {
        // @todo: most likely a really shitty hash function.
        // we should probably use a better hash_combine, or maybe go hard with
        // FNV-1a hashing.
        auto h1 = std::hash<VkPipelineLayoutCreateFlags>{}(info.flags);
        auto h2 = std::hash<u32>{}(info.descriptor_set_layouts.len);
        auto h3 = std::hash<u32>{}(info.push_constant_ranges.len);

        auto h = h1 ^ (h2 << 1) ^ (h3 << 2);
        for (u32 i = 0; i < info.descriptor_set_layouts.len; i++) {
            h ^= (std::hash<VkDescriptorSetLayout>{}(info.descriptor_set_layouts[i]) << i);
        }
        for (u32 i = 0; i < info.push_constant_ranges.len; i++) {
            auto &range = info.push_constant_ranges[i];
            h ^= (std::hash<VkShaderStageFlags>{}(range.stageFlags) << i);
            h ^= (std::hash<u32>{}(range.offset) << i);
            h ^= (std::hash<u32>{}(range.size) << i);
        }

        return h;
    }
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
    void write_buffer(u32 binding, u32 array_index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);
private:
    std::deque<VkDescriptorImageInfo> image_infos;
    std::deque<VkDescriptorBufferInfo> buffer_infos;
    std::vector<VkWriteDescriptorSet> writes;
};
