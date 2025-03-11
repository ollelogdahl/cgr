#pragma once

#include "linalg.h"
#include "rend2/buffer.h"
#include "rend2/descriptor_set.h"
#include "rend2/shader_compiler.h"
#define MAX_LIGHTS_PER_CLUSTER 31

// Clustered Forward Shading
// https://www.cse.chalmers.se/~uffe/clustered_shading_preprint.pdf
class ClusterShading {
public:
    ClusterShading(gpu_t &gpu, ShaderCompiler &sc, u32 x = 16, u32 y = 9, u32 z = 24);

    struct alignas(16) ClusterData {
        v4f bounds_min;
        v4f bounds_max;
        u32 num_lights;
        u32 light_indices[MAX_LIGHTS_PER_CLUSTER];
    };

    u32 num_clusters() const { return m_x * m_y * m_z; }

    // can be called only when projection changes.
    // @todo: we can likely start sharing a view-buffer containing all
    // view related data. Oh well.
    void rebuild_clusters(CommandBuffer &cmd, f32 znear, f32 zfar, const m4f &m_inv_proj);

    GpuBuffer &cluster_buffer() { return m_cluster_buffer; }
private:
    gpu_t &m_gpu;

    u32 m_x = 16;
    u32 m_y = 9;
    u32 m_z = 24;

    GpuBuffer m_cluster_buffer;

    DescriptorSet m_descriptor_set;

    VkPipelineLayout m_gen_pipeline_layout;
    VkPipeline m_gen_pipeline;
};
