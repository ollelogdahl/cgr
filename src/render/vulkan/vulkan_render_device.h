pragma once
#include "render_device.h"
#include "vulkan_render_context.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

// Forward declarations
struct VulkanCommandNode;

class VulkanFrameGraph {
public:
    explicit VulkanFrameGraph(VkDevice device, VkQueue graphics_queue, VkQueue compute_queue);

    VulkanCommandNode* create_command_node();
    void add_dependency(VulkanCommandNode* before, VulkanCommandNode* after);

    // Called when submitting the frame
    void execute();

private:
    struct Node {
        VulkanCommandNode* cmd_node;
        std::vector<Node*> dependencies;
        std::vector<Node*> dependents;
    };

    std::vector<std::unique_ptr<Node>> nodes;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue compute_queue;

    void create_synchronization_primitives();
    void topological_sort();
};

class VulkanRenderDevice : public RenderDevice {
public:
    explicit VulkanRenderDevice(VulkanRenderContext& context);
    ~VulkanRenderDevice() override;

    // RenderDevice interface implementation
    ComputePipeline create_compute_pipeline(Shader& shader) override;
    DrawPipeline create_draw_pipeline(Shader& shader) override;

    Buffer create_uniform_buffer(u64 size) override;
    Buffer create_storage_buffer(u64 size) override;
    Buffer create_vertex_buffer(u64 size) override;
    Buffer create_index_buffer(u64 size) override;

    UniformSet create_uniform_set(std::span<UniformBindingDecl> bindings) override;

    ComputeList begin_compute_list() override;
    void end_compute_list(ComputeList& list) override;

    DrawList begin_draw_list_for_screen() override;
    void end_draw_list(DrawList& list) override;

    void submit() override;

protected:
    void buffer_clear(Buffer& buffer, u64 offset, u64 size) override;
    void buffer_copy(Buffer& src, Buffer& dst, u64 src_offset, u64 dst_offset, u64 size) override;
    std::span<byte> buffer_read(Buffer& buffer, u64 offset, u64 size) override;
    void buffer_write(Buffer& buffer, u64 offset, std::span<byte> data) override;

private:
    VkDevice device;
    VkQueue graphics_queue;
    // @todo: for now we use a single queue. We could in theory use multiple.
    VkQueue compute_queue;
    VkCommandPool command_pool;

    VulkanFrameGraph frame_graph;

    // Helper methods
    VkBuffer create_buffer(VkBufferUsageFlags usage, u64 size, VkMemoryPropertyFlags properties);
    void allocate_buffer_memory(VkBuffer buffer, VkDeviceMemory& memory);
};
