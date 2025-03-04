#pragma once

#include "linalg.h"
#include "rend2/buffer.h"
#include "rend2/command_buffer.h"
#include "rend2/descriptor_set.h"
#include <vulkan/vulkan_core.h>

#include "shader_compiler.h"

class CullComputePass {
public:
    CullComputePass(gpu_t &gpu, u32 max_batches, ShaderCompiler &sc);

    void bind_buffers(const GpuBuffer &object_buffer, const GpuBuffer &draw_buffer, const GpuBuffer &mesh_buffer);

    void update_view(const v3f &eye, const m4f &vp);

    // complexity: to render the CPU needs to know the offset into the draw buffer a certain batch
    // begins. Therefore, I think it is reasonable to partition the draw buffer from the CPU side.
    // This can be done by knowing the number of elements in each batch. Kinda funky as we likely cull
    // most objects, meaning that the draw buffer must be same size as the object buffer. Well well.
    //
    // The batch start_index is the byte offset into which this batch starts. This includes the size of the
    // batch, so the first draw command is at start_index + sizeof(u32).
    struct Batch {
        u32 start_index;
        u32 count;
    };
    void assign_batches(std::span<Batch> batches);

    void record(CommandBuffer &cmd, bool use_batches);
private:

    struct CullData {
        v4f planes[6];
        v3f eye;
    };

    gpu_t *m_gpu;
    DescriptorSet m_descriptor_set;

    const GpuBuffer *m_draw_buffer;

    u32 m_object_count;

    // @todo: do we want to get this from the gpu?
    // can we specialize the shader for a specific workgroup size?
    u32 m_workgroup_size = 64;

    std::vector<Batch> m_batches;
    bool m_batches_dirty = false;
    GpuBuffer m_batch_buffer;

    CullData m_cull_data;
    bool m_cull_data_dirty = false;
    GpuBuffer m_cull_buffer;
    GpuBuffer m_stats_buffer;

    VkPipelineLayout m_pipeline_layout;
    VkPipeline m_pipeline;
};
