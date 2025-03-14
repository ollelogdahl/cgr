#pragma once

#include "linalg.h"
#include "rend2/buffer.h"
#include "rend2/descriptor_set.h"
#include "rend2/shader_compiler.h"

// Clustered Forward Shading
// https://www.cse.chalmers.se/~uffe/clustered_shading_preprint.pdf
// https://advances.realtimerendering.com/s2016/Siggraph2016_idTech6.pdf
//
// This system is inspired by IdTech 6's implementation of clustered shading.
//
// Currently, only light items are supported. In the future, decals and probes
// can be added.
//
//
// In IdTech 7 items are bound by an hexahedra.
//
// see also:
// https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf
struct ClusterConfig {
    u32 grid_x = 16;
    u32 grid_y = 9;
    u32 grid_z = 24;

    u32 max_items_per_cluster = 256;

    const GpuBuffer &light_buffer;
};

class ClusterShading {
public:
    ClusterShading(gpu_t &gpu, ShaderCompiler &sc, const ClusterConfig &config);

    struct alignas(16) ClusterData {
        v4f bounds_min;
        v4f bounds_max;
        u32 item_start;
        u32 hash_and_num_lights;
    };

    void set_light_threshold(f32 v) { runtime_config.light_threshold = v; }

    u32 num_clusters() const { return m_config.grid_x * m_config.grid_y * m_config.grid_z; }

    // can be called only when projection changes.
    // @todo: we can likely start sharing a view-buffer containing all
    // view related data. Oh well.
    void rebuild_clusters(CommandBuffer &cmd, f32 znear, f32 zfar, const m4f &m_inv_proj);

    // @todo: how do we upload items to here? We reuse lights from RenderStorage for now.
    void assign_items(CommandBuffer &cmd, const m4f &view_matrix);

    GpuBuffer &cluster_buffer() { return m_cluster_buffer; }
    GpuBuffer &cluster_item_buffer() { return m_items_buffer; }
private:
    gpu_t &m_gpu;

    ClusterConfig m_config;

    GpuBuffer m_cluster_buffer;
    GpuBuffer m_items_buffer;

    DescriptorSet m_gen_descriptor_set;
    DescriptorSet m_assign_descriptor_set;

    struct {
        f32 light_threshold = 1.0 / 40.0f;
    } runtime_config;

    VkPipelineLayout m_gen_pipeline_layout;
    VkPipelineLayout m_assign_pipeline_layout;
    VkPipeline m_gen_pipeline;
    VkPipeline m_assign_pipeline;
};
