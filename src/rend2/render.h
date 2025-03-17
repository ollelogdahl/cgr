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


class Renderer {
public:

    Renderer(gpu_t &gpu, RenderStorage &storage, ShaderCompiler &sc);

    ForwardMeshPass &forward_pass() { return m_forward_pass; }
    ClusterShading &cluster_shading() { return m_cluster_shading; }

    // render
    void render(gpu_t::frame_t &frame, const View &view);

    RenderState &state() { return m_state; }
private:
    gpu_t *m_gpu;
    RenderStorage &m_storage;
    RenderState m_state;
    ShaderCompiler &m_shader_compiler;

    GpuBuffer m_draw_buffer;
    ForwardMeshPass m_forward_pass;
    ShadowMeshPass m_shadow_pass;

    ClusterShading m_cluster_shading;
};
