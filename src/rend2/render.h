#pragma once

#include "model.h"
#include "oc.h"
#include "rend2/forward_mesh_pass.h"
#include "rend2/render_handles.h"
#include "rend2/render_state.h"

#include "rend2/shadow_mesh_pass.h"
#include "shader_compiler.h"

#include "command_buffer.h"
#include <vulkan/vulkan_core.h>

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

    Renderer(gpu_t &gpu, ShaderCompiler &sc);

    // resources
    // @todo: material redesign.
    // There is actually nothing enforcing us to ue the same struct for all materials. We can probably be smart here.
    MeshHandle add_mesh(const Mesh &);
    MaterialHandle add_material(const MaterialData &data);
    ShaderHandle load_shader(const LoadShaderProperties &props);

    // objects
    ObjectHandle add_object();
    void delete_object(ObjectHandle handle);

    void assign_geometry(ObjectHandle handle, MeshHandle mesh);
    void assign_material(ObjectHandle handle, MaterialHandle material);
    void assign_shader(ObjectHandle handle, ShaderHandle shader);
    void update_transform(ObjectHandle handle, const m4f &transform);

    void update_global(const GlobalData &data);

    // @todo: see note in render state...
    void set_lights(std::span<const LightData> lights) {
        m_state.set_lights(lights);
    }

    // render
    void render(gpu_t::frame_t &frame, const View &view);
private:
    gpu_t *m_gpu;
    RenderState m_state;
    ShaderCompiler &m_shader_compiler;

    VkPipelineLayout m_forward_pipeline_layout;

    GpuBuffer m_draw_buffer;
    ForwardMeshPass m_forward_pass;
    ShadowMeshPass m_shadow_pass;

    gpu_image_t m_shadow_map_image;

    struct ShaderInfo {
        VkPipeline render_pipeline;
    };

    std::unordered_map<LoadShaderProperties, ShaderHandle> m_shader_cache;
    std::vector<ShaderInfo> m_shaders; // @todo: stop using the old gpu_shader_t type!
};
