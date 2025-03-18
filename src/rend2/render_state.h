#pragma once

#include "model.h"
#include "rend2/command_buffer.h"
#include "rend2/render_handles.h"
#include "rend2/render_storage.h"
#include "rend2/shader_compiler.h"

#include <set>

struct LoadShaderProperties {
    const char *glsl_vert_path;
    const char *glsl_frag_path;
};

enum class TextureType {
    R,
    RGB,
    SRGB,
    RGBA,
    SRGBA,
};

struct LoadTextureProperties {
    const char *path;
    TextureType type;
};

bool operator==(const LoadShaderProperties &lhs, const LoadShaderProperties &rhs);
template <> struct std::hash<LoadShaderProperties> {
    std::size_t operator()(const LoadShaderProperties &props) const;
};

bool operator==(const LoadTextureProperties &lhs, const LoadTextureProperties &rhs);
template <> struct std::hash<LoadTextureProperties> {
    std::size_t operator()(const LoadTextureProperties &props) const;
};

// we could call this something else. This is the main interface
// to GPU stored resources.
class RenderState {
public:
    RenderState(gpu_t &gpu, RenderStorage &storage, ShaderCompiler &sc)
    : m_gpu(gpu), m_storage(storage), m_shader_compiler(sc) {}

    // delete copy and move
    RenderState(const RenderState &) = delete;
    RenderState &operator=(const RenderState &) = delete;
    RenderState(RenderState &&) = delete;
    RenderState &operator=(RenderState &&) = delete;

    MeshHandle add_mesh(const Mesh &);
    MaterialHandle add_material(const MaterialData &data);
    ShaderHandle load_shader(const LoadShaderProperties &props);

    TextureHandle load_texture(const LoadTextureProperties &props);

    void update_material(MaterialHandle handle, const MaterialData &data);

    // objects
    ObjectHandle add_object();

    void assign_geometry(ObjectHandle handle, MeshHandle mesh);
    void assign_material(ObjectHandle handle, MaterialHandle material);
    void assign_shader(ObjectHandle handle, ShaderHandle shader);

    void update_transform(ObjectHandle handle, const m4f &transform);

    LightHandle add_light(const LightData &data);
    void update_light(LightHandle handle, const LightData &data);

    void finalize_before_render(CommandBuffer &cmd);

    // @todo: clean this up, this sucks!
    void set_forward_pipeline_layout(VkPipelineLayout layout) {
        m_forward_pipeline_layout = layout;
    }
private:
    void mark_object_for_tlas_update(ObjectHandle handle);
    void update_tlas(CommandBuffer &cmd);

    struct BlasBuildTask {
        MeshHandle handle;
        MeshData data;
        u32 num_vertices;
    };

    gpu_t &m_gpu;
    RenderStorage &m_storage;
    ShaderCompiler &m_shader_compiler;

    VkPipelineLayout m_forward_pipeline_layout;

    std::vector<BlasBuildTask> m_blas_tasks;

    bool m_tlas_initialized = false;
    std::set<ObjectHandle> m_objects_needing_tlas_update;
    std::unordered_map<MeshHandle, AccelerationBLAS> m_mesh_to_blas;

    std::unordered_map<LoadShaderProperties, ShaderHandle> m_shader_cache;
    std::unordered_map<LoadTextureProperties, TextureHandle> m_texture_cache;
};
