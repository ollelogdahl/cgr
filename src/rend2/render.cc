#include "render.h"
#include "gpu.h"
#include "metrics.h"
#include "model.h"
#include "rend2/buffer.h"
#include "rend2/forward_indirect_pass.h"
#include "rend2/render_state.h"
#include "pipeline_layout_builder.h"
#include "descriptor_set_layout_builder.h"

#include <algorithm>

#include "rend2/shader_compiler.h"
#include "rend2/vku.h"
#include "tracy/Tracy.hpp"

static const RenderStateConfig config = {
    .max_objects = 100 * 1024,
    .max_vertices = 100 * 1024,
    .max_indices = 100 * 1024,
    .max_meshes = 1024,
    .max_materials = 1024,
    .max_textures = 1024,
};
static const u32 max_draws = config.max_objects;

Renderer::Renderer(gpu_t &gpu, ShaderCompiler &sc) : m_gpu(&gpu), m_state(gpu, config),
        cull_pass(gpu, sc), forward_pass(gpu, sc, config.max_textures),
    m_draw_buffer(gpu, max_draws * sizeof(DrawCommand) + 1 * sizeof(u32),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
    {

    cull_pass.bind_buffers(m_state.object_buffer(), m_draw_buffer, m_state.mesh_buffer());
    forward_pass.set_resources(
        m_state.global_buffer().get(),
        m_state.object_buffer().get(),
        m_draw_buffer.get(),
        m_state.material_buffer().get()
    );
}

MeshHandle Renderer::add_mesh(const Mesh &mesh) {
    // 1. interleave vertex properties.
    // 2. calculate the index offset (where the vertices were allocated), and offset all indices.

    // @todo: Using manual vertex fetching, we could actually support different
    // types of vertex data. This could be really nice, as we could save memory.
    // According to some sources, manual fetching is not too slow.

    bool uv_present = mesh.uvs.size() > 0;
    bool color_present = mesh.colors.size() > 0;
    auto interleaved = std::vector<byte>(mesh.vertices.size() * 9 * sizeof(f32));
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        auto &v = mesh.vertices[i];
        auto &n = mesh.normals[i];


        memcpy(&interleaved[i * 9 * sizeof(f32)], &v, sizeof(v));
        memcpy(&interleaved[i * 9 * sizeof(f32) + 3 * sizeof(f32)], &n, sizeof(n));

        if (uv_present) {
            auto &t = mesh.uvs[i];
            memcpy(&interleaved[i * 9 * sizeof(f32) + 6 * sizeof(f32)], &t, sizeof(t));
        }

        if (color_present) {
            auto &c = mesh.colors[i];
            memcpy(&interleaved[i * 9 * sizeof(f32) + 8 * sizeof(f32)], &c, sizeof(c));
        }
    }

    auto vertex_handle = m_state.alloc_vertices(slice<byte>(interleaved));

    auto idx_handles = std::vector<IndexDataHandle>();
    idx_handles.reserve(mesh.lods.size());

    // offset all indices
    for (auto &lod : mesh.lods) {
        // make a complete copy :^)
        auto indices = std::vector<u32>(lod.indices.size());
        indices = lod.indices;

        std::transform(indices.begin(), indices.end(), indices.begin(),
            [&](u32 idx) { return idx + vertex_handle.idx; });

        idx_handles.push_back(
            m_state.alloc_indices(std::move(indices))
        );
    }

    assert(mesh.lods.size() > 0);
    // @note: as MeshData currently works, this makes the vertex-data handle actually dangling.
    // In my current scenario this is fine (as i think we should try automatic resource reclaim),
    // but maybe not in the future.
    auto mesh_data = MeshData{};
    for (size_t i = 0; i < 4; i++) {
        auto src_idx = std::min(i, mesh.lods.size() - 1);
        mesh_data.lods[i] = {
            .index_start = idx_handles[src_idx].idx,
            .index_count = idx_handles[src_idx].size,
            .distance = mesh.lods[src_idx].min_distance,
        };
    }

    mesh_data.bounds_min = mesh.bounds.min.to_homogeneous();
    mesh_data.bounds_max = mesh.bounds.max.to_homogeneous();

    auto mesh_handle = m_state.alloc_mesh(mesh_data);
    return mesh_handle;
}

MaterialHandle Renderer::add_material(const MaterialData &data) {
    return m_state.alloc_material(data);
}

ObjectHandle Renderer::add_object() {
    // each object has a unique transform for now
    auto object = m_state.alloc_object();

    return object;
}

void Renderer::assign_geometry(ObjectHandle handle, MeshHandle mesh) {
    auto old = m_state.object_data(handle);
    old.mesh = mesh;
    m_state.update_object(handle, old);
}

void Renderer::assign_material(ObjectHandle handle, MaterialHandle material) {
    auto old = m_state.object_data(handle);
    old.material = material;
    m_state.update_object(handle, old);
}

void assign_packed_affine_transformation(f32 *dest, const m4f &m) {
    // column-major
    dest[0] = m.m[0];
    dest[1] = m.m[1];
    dest[2] = m.m[2];
    dest[3] = m.m[4];
    dest[4] = m.m[5];
    dest[5] = m.m[6];
    dest[6] = m.m[8];
    dest[7] = m.m[9];
    dest[8] = m.m[10];
    dest[9] = m.m[12];
    dest[10] = m.m[13];
    dest[11] = m.m[14];
}

void Renderer::update_transform(ObjectHandle handle, const m4f &t) {
    auto o = m_state.object_data(handle);
    assign_packed_affine_transformation(o.transform, t);
    m_state.update_object(handle, o);
}

void Renderer::update_global(const GlobalData &data) {
    m_state.update_global(data);

    m4f vp = data.view * data.proj;
    cull_pass.update_view(data.view_pos, vp);
}

void Renderer::render(gpu_t::frame_t &frame) {
    ZoneScoped;
    auto state_dependencies = m_state.flush(frame.cmd);

    // @todo: move this!
    // We invoke a compute shader which performs copies from the Object Buffer to
    // the Draw Buffer. It only copies if the objects are visible.
    // Therefore, we need to pass some cull information in a ubo or something.
    // This is actually recording to a different command buffer (and queue potentially)

    // @todo: consider different shaders! This is tricky. We want a DrawIndirect command for
    // each shader, and we don't want them to wait. Therefore, we need to either
    //      1. Partition the draw buffer by shader
    //      2. Have a separate draw buffer for each shader
    //
    // We maybe also should separate the object buffers by shader. This would make things
    // WAAAY easier i think. In that case, the culling and stuff does not need to care about
    // those details.
    WriteDependency cull_modified;
    {
        TracyVkZone(frame.cmd.tracy_ctx(), frame.cmd.get(), "wait-state-change");
        WriteDependency dependencies;
        dependencies.join(state_dependencies.objects);
        dependencies.join(state_dependencies.meshes);

        dependencies.pipeline_barrier(frame.cmd.get(),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    }

    {
        cull_pass.record(frame.cmd, m_state.highest_object_id() + 1);

        VkBufferMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barrier.buffer = m_draw_buffer.get();
        barrier.size = VK_WHOLE_SIZE;
        cull_modified.barriers.push_back(barrier);
    }

    // now we want to make another barrier. I guess in some way, the cull pass should
    // also return an array of changes done. We can call it ResourcePoke.
    {
        TracyVkZone(frame.cmd.tracy_ctx(), frame.cmd.get(), "wait-cull-pass");
        cull_modified.pipeline_barrier(frame.cmd.get(),
            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    }


    RenderTarget target = {
        .color_view = m_gpu->swapchain.image_views[frame.image_idx],
        .depth_view = m_gpu->depth_image.view,
        .extent = m_gpu->swapchain.extent,
    };

    VkBuffer vertex_buffers[] = {m_state.vertex_buffer().get()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(frame.cmd.get(), 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(frame.cmd.get(), m_state.index_buffer().get(), 0, VK_INDEX_TYPE_UINT32);

    std::vector<IndirectBatch> batches = {
        {
            .pipeline = 0,
            .index = 0,
            .count = 1,
            .max_draws = max_draws,
        }
    };

    forward_pass.record(frame.cmd, target, batches);
}

bool operator==(const LoadShaderProperties &lhs, const LoadShaderProperties &rhs) {
    return lhs.glsl_vert_path == rhs.glsl_vert_path && lhs.glsl_frag_path == rhs.glsl_frag_path;
}

std::size_t std::hash<LoadShaderProperties>::operator()(const LoadShaderProperties &props) const {
    return std::hash<const char *>()(props.glsl_vert_path) ^ std::hash<const char *>()(props.glsl_frag_path);
}
