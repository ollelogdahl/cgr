#pragma once

#include "gpu.h"
#include "rend2/cull_compute_pass.h"
#include "rend2/render_state.h"
#include "rend2/render_target.h"
#include "rend2/shader_compiler.h"
#include "rend2/view.h"
#include <vulkan/vulkan_core.h>

class ShadowMeshPass {
public:
    ShadowMeshPass(gpu_t &gpu, ShaderCompiler &sc,
        RenderState &state,
        GpuBuffer &draw_buffer);

    void set_bias_constant(f32 bias) { m_bias_constant = bias; }
    void set_bias_slope(f32 bias_slope) { m_bias_slope = bias_slope; }
    void set_bias_clamp(f32 bias_clamp) { m_bias_clamp = bias_clamp; }

    void record(CommandBuffer &cmd, RenderTarget& target, const View& view, u32 max_count);
private:
    gpu_t &m_gpu;
    RenderState &m_state;

    GpuBuffer &m_draw_buffer;

    VkPipelineLayout m_pipeline_layout;
    VkPipeline m_pipeline;

    CullComputePass m_cull_pass;

    f32 m_bias_constant = 0.001f;
    f32 m_bias_slope = 0.03f;
    f32 m_bias_clamp = 0.1f;
};
