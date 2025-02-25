#pragma once

#include "oc.h"
#include "rend2/cull_compute_pass.h"
#include "rend2/render_handles.h"
#include "rend2/render_state.h"
#include "resource.h"

#include "command_buffer.h"

struct LoadShaderProperties {
    const char *glsl_vert_path;
    const char *glsl_frag_path;
};

bool operator==(const LoadShaderProperties &lhs, const LoadShaderProperties &rhs);
template <> struct std::hash<LoadShaderProperties> {
    std::size_t operator()(const LoadShaderProperties &props) const;
};

class Renderer {
public:
    Renderer(gpu_t &gpu);

    // resources
    MeshHandle add_mesh(const Mesh &);
    MaterialHandle add_material(const MaterialData &data);
    ShaderHandle load_shader(const LoadShaderProperties &props);

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

    // @todo: break out!
    CullComputePass cull_pass;

    std::unordered_map<LoadShaderProperties, ShaderHandle> shader_cache;
    std::vector<gpu_shader_t> shaders; // @todo: stop using the old gpu_shader_t type!
};
