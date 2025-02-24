#include "render.h"
#include "rend2/render_state.h"

#include <algorithm>

static const RenderStateConfig config = {
    .max_objects = 1024,
    .max_vertices = 1 * 1024 * 1024,
    .max_indices = 1 * 1024 * 1024,
    .max_meshes = 1024,
    .max_materials = 1024,
    .max_textures = 1024,
};
static const u32 max_draws = 1024;

struct DrawCommand {
    u32 object_id;
    VkDrawIndexedIndirectCommand indirect;
};

Renderer::Renderer(gpu_t &gpu) : m_gpu(&gpu), m_state(gpu, config) {
    m_draw_buffer = GpuBuffer(gpu, max_draws * sizeof(DrawCommand),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    m_draw_count_buffer = GpuBuffer(gpu, max_draws * sizeof(u32),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

MeshHandle Renderer::add_mesh(const Mesh &mesh) {
    // 1. interleave vertex properties.
    // 2. calculate the index offset (where the vertices were allocated), and offset all indices.

    // @todo: Using manual vertex fetching, we could actually support different
    // types of vertex data. This could be really nice, as we could save memory.
    // According to some sources, manual fetching is not too slow.

    auto interleaved = std::vector<byte>(mesh.vertices.size() * 9 * sizeof(f32));
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        auto &v = mesh.vertices[i];
        auto &n = mesh.normals[i];
        auto &t = mesh.uvs[i];
        auto &c = mesh.colors[i];

        memcpy(&interleaved[i * 9 * sizeof(f32)], &v, sizeof(v));
        memcpy(&interleaved[i * 9 * sizeof(f32) + 3 * sizeof(f32)], &n, sizeof(n));
        memcpy(&interleaved[i * 9 * sizeof(f32) + 6 * sizeof(f32)], &t, sizeof(t));
        memcpy(&interleaved[i * 9 * sizeof(f32) + 8 * sizeof(f32)], &c, sizeof(c));
    }

    auto vertex_handle = m_state.alloc_vertices(slice<byte>(interleaved));

    auto idx_handles = std::vector<IndexDataHandle>(mesh.lods.size());

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

    assert(mesh.lods.size() >= 4);

    // @note: as MeshData currently works, this makes the vertex-data handle actually dangling.
    // In my current scenario this is fine (as i think we should try automatic resource reclaim),
    // but maybe not in the future.
    auto mesh_data = MeshData{
        .lods = {
            { .index_start = idx_handles[0].idx, .index_count = idx_handles[0].size },
            { .index_start = idx_handles[1].idx, .index_count = idx_handles[1].size },
            { .index_start = idx_handles[2].idx, .index_count = idx_handles[2].size },
            { .index_start = idx_handles[3].idx, .index_count = idx_handles[3].size },
        }
    };

    auto mesh_handle = m_state.alloc_mesh(mesh_data);
    return mesh_handle;
}

MaterialHandle Renderer::add_material(const MaterialData &data) {
    return m_state.alloc_material(data);
}

ObjectHandle Renderer::add_object() {
    // each object has a unique transform for now
    auto object = m_state.alloc_object();

    m_state.update_object(object, {
        .material = MaterialHandle(),
        .mesh = MeshHandle(),
        .transform = m4f::identity(),
    });

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

void Renderer::update_transform(ObjectHandle handle, const m4f &transform) {
    auto o = m_state.object_data(handle);
    o.transform = transform;
    m_state.update_object(handle, o);
}

void Renderer::render(gpu_t::frame_t &frame) {
    m_state.flush(frame.cmds);

    // for now, don't do any indirect draws. Just draw as we have done previously.

    // figure out the different shader batches.
    // for now, only support one shader.
    VkBuffer vertex_buffers[] = {m_state.vertex_buffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(frame.cmds, 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(frame.cmds, m_state.index_buffer(), 0, VK_INDEX_TYPE_UINT32);

    // bind the global descriptor set
    VkDescriptorSet descriptor_sets[] = {m_state.global_descriptor_set()};
    vkCmdBindDescriptorSets(frame.cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, 0,
        1, descriptor_sets, 0, nullptr);


}
