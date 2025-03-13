#pragma once

#include "model.h"
#include "oc.h"
#include "rend2/cluster.h"
#include "rend2/forward_mesh_pass.h"
#include "rend2/render_handles.h"
#include "rend2/render_storage.h"

#include "rend2/shadow_mesh_pass.h"
#include "shader_compiler.h"

#include "command_buffer.h"
#include <vulkan/vulkan_core.h>

class Renderer {
public:

    Renderer(gpu_t &gpu, RenderStorage &storage, ShaderCompiler &sc);

    RenderStorage::ShaderInfo create_pipelines_for(Shader &shader);

    ForwardMeshPass &forward_pass() { return m_forward_pass; }
    ClusterShading &cluster_shading() { return m_cluster_shading; }

    // render
    void render(gpu_t::frame_t &frame, const View &view);
private:
    gpu_t *m_gpu;
    RenderStorage &m_storage;
    ShaderCompiler &m_shader_compiler;

    VkPipelineLayout m_forward_pipeline_layout;

    GpuBuffer m_draw_buffer;
    ForwardMeshPass m_forward_pass;
    ShadowMeshPass m_shadow_pass;

    ClusterShading m_cluster_shading;
};
