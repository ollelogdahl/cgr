#pragma once

#include "rend2/buffer.h"
#include "rend2/command_buffer.h"
#include "rend2/descriptor_set.h"
#include <vulkan/vulkan_core.h>
class CullComputePass {
public:
    CullComputePass(gpu_t &gpu);

    void bind_buffers(const GpuBuffer &object_buffer, const GpuBuffer &draw_buffer, const GpuBuffer &mesh_buffer);

    void run(WriteDependency &depends_on, u32 object_count);

    CommandBuffer &cmd() { return m_cmd; }
private:
    gpu_t *m_gpu;
    CommandBuffer m_cmd;
    DescriptorSet m_descriptor_set;
    VkFence m_fence;

    VkPipelineLayout m_pipeline_layout;
    VkPipeline m_pipeline;
};
