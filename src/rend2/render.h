#pragma once

#include "rend2/render_handles.h"
#include "rend2/render_state.h"
#include "resource.h"

class Renderer {
public:
    Renderer(gpu_t &gpu);

    // resources
    MeshHandle add_mesh(const Mesh &);
    MaterialHandle add_material(const MaterialData &data);

    // objects
    ObjectHandle add_object();
    void delete_object(ObjectHandle handle);

    void assign_geometry(ObjectHandle handle, MeshHandle mesh);
    void assign_material(ObjectHandle handle, MaterialHandle material);
    void update_transform(ObjectHandle handle, const m4f &transform);

    // render
    void render(gpu_t::frame_t &frame);
private:
    gpu_t *m_gpu;
    RenderState m_state;

    VkPipelineLayout m_pipeline_layout;

    GpuBuffer m_draw_buffer;
    GpuBuffer m_draw_count_buffer;
};
