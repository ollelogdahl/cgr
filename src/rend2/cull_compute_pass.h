#pragma once

#include "linalg.h"
#include "rend2/buffer.h"
#include "rend2/command_buffer.h"
#include "rend2/descriptor_set.h"


#include "shader_compiler.h"

class CullComputePass {
public:
    CullComputePass(gpu_t &gpu, ShaderCompiler &sc);

    void bind_buffers(const GpuBuffer &object_buffer, const GpuBuffer &draw_buffer, const GpuBuffer &mesh_buffer);

    void update_view(const v3f &eye, const m4f &vp);

    void record(CommandBuffer &cmd, u32 object_count);
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

    CullData m_cull_data;
    bool m_cull_data_dirty = false;
    GpuBuffer m_cull_buffer;
    GpuBuffer m_stats_buffer;

    VkPipelineLayout m_pipeline_layout;
    VkPipeline m_pipeline;
};
