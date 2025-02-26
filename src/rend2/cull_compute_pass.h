#pragma once

#include "rend2/buffer.h"
#include "rend2/command_buffer.h"
#include "rend2/descriptor_set.h"
#include <vulkan/vulkan_core.h>
class CullComputePass {
public:
    CullComputePass(gpu_t &gpu);

    void bind_buffers(const GpuBuffer &object_buffer, const GpuBuffer &draw_buffer, const GpuBuffer &mesh_buffer);

    void record(CommandBuffer &cmd, u32 object_count);
private:
    gpu_t *m_gpu;
    DescriptorSet m_descriptor_set;

    const GpuBuffer *m_draw_buffer;

    VkPipelineLayout m_pipeline_layout;
    VkPipeline m_pipeline;
};
