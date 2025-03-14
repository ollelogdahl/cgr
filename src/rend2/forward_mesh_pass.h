#pragma once

#include "rend2/buffer.h"
#include "rend2/cull_compute_pass.h"
#include "rend2/render_state.h"
#include "rend2/render_target.h"
#include "rend2/view.h"
#include <vulkan/vulkan_core.h>

#include "rend2/pipeline_query.h"

// @todo: make a base MeshPass

// @todo: inter-mesh pass dependency tracking!
// dont know how though.

struct IndirectBatch2 {
    VkPipeline pipeline;
    u32 buffer_offset;
    u32 count;
};

enum class DebugMode {
    None,
    Unlit,
    SkipClusterShading,
    ClusterScalarRead,
    ClusterLights,
    ClusterHash,
};

class ForwardMeshPass {
public:
    ForwardMeshPass(gpu_t &gpu, ShaderCompiler &sc,
        RenderStorage &state,
        GpuBuffer &draw_buffer);

    VkPipelineLayout pipeline_layout() const { return m_pipeline_layout; }

    void set_debug_mode(DebugMode mode) { m_debug = mode; }

    void record(CommandBuffer &cmd, RenderTarget& target, const View& view,
        std::span<IndirectBatch2> batches);
private:
    gpu_t &m_gpu;
    RenderStorage &m_state;

    GpuBuffer &m_draw_buffer;

    VkPipelineLayout m_pipeline_layout;

    CullComputePass m_cull_pass;

    PipelineQuery m_pipeline_query;

    DebugMode m_debug = DebugMode::None;
};
