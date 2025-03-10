#pragma once

#include "model.h"
#include "rend2/render_handles.h"
#include "rend2/render_storage.h"
#include "rend2/shader_compiler.h"

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
    RenderState(gpu_t &gpu, RenderStorage &storage, Renderer &rend, ShaderCompiler &sc)
    : m_gpu(gpu), m_storage(storage), m_renderer(rend), m_shader_compiler(sc) {}

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
private:
    gpu_t &m_gpu;
    RenderStorage &m_storage;
    Renderer &m_renderer;
    ShaderCompiler &m_shader_compiler;

    std::unordered_map<LoadShaderProperties, ShaderHandle> m_shader_cache;
    std::unordered_map<LoadTextureProperties, TextureHandle> m_texture_cache;
};
