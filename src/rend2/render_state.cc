#include "render_state.h"
#include "metrics.h"
#include "rend2/render_handles.h"
#include "descriptor_set_layout_builder.h"
#include "rend2/vku.h"

#include "log.h"

#include <tracy/Tracy.hpp>

const u32 vertex_size = 9 * sizeof(f32);
const u32 index_size = sizeof(u32);
const u32 material_size = 32;

logger_t logger = logger_t("renderstate");

RenderState::RenderState(gpu_t &gpu, const RenderStateConfig &config)
:
    // @todo: make some into storage buffers instead
    m_gpu(&gpu),
    m_object_buffer(gpu, config.max_objects * sizeof(ObjectData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    m_object_data(new ObjectData[config.max_objects]),
    m_vertex_buffer(gpu, config.max_vertices * vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
    m_index_buffer(gpu, config.max_indices * index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
    m_material_buffer(gpu, config.max_materials * material_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    m_mesh_buffer(gpu, config.max_meshes * sizeof(MeshData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    m_global_buffer(gpu, sizeof(GlobalData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    m_vertex_alloc(config.max_vertices),
    m_index_alloc(config.max_indices),
    m_material_alloc(config.max_materials),
    m_mesh_alloc(config.max_meshes),
    m_texture_alloc(config.max_textures),
    m_object_alloc(config.max_objects) {

    u64 gpu_total_size_bytes = m_object_buffer.size() + m_vertex_buffer.size() + m_index_buffer.size() +
        m_material_buffer.size() + m_mesh_buffer.size() + m_global_buffer.size();
    metrics::gauge_u64("rend2.state.gpu_size", gpu_total_size_bytes, "b");

    // write default values to the object buffer
    {
        // column-major m34f identity matrix
        ObjectData default_object = {
            .transform = {
                1, 0, 0,
                0, 1, 0,
                0, 0, 1,
                0, 0, 0,
            },
            .material = -1,
            .mesh = -1,
        };


        for (u32 i = 0; i < config.max_objects; ++i) {
            m_object_data[i] = default_object;
        }

        m_object_buffer.write(slice<byte>((byte *)m_object_data, config.max_objects * sizeof(ObjectData)));
    }
}

VertexDataHandle RenderState::alloc_vertices(slice<byte> vertices) {
    ZoneScoped;
    u32 num_vertices = vertices.len / vertex_size;
    u32 start = m_vertex_alloc.allocate(num_vertices);
    if (start == -1U) {
        logger.error("failed to allocate {} vertices", num_vertices);
        return { -1U, 0 };
    }

    counts.vertices += num_vertices;

    m_writeback_buffers.vertices.insert({start * vertex_size, {vertices.begin(), vertices.end()}});

    return { start, num_vertices };
}

IndexDataHandle RenderState::alloc_indices(std::vector<u32> &&indices) {
    ZoneScoped;
    u32 start = m_index_alloc.allocate(indices.size());
    if (start == -1U) {
        logger.error("failed to allocate {} indices", indices.size());
        return { -1U, 0 };
    }

    counts.indices += indices.size();

    u32 written = indices.size();
    m_writeback_buffers.indices.insert({start * (u32)sizeof(u32), std::move(indices)});

    return { start, written };
}

MeshHandle RenderState::alloc_mesh(const MeshData &data) {
    ZoneScoped;
    u32 idx = m_mesh_alloc.allocate();
    if (idx == -1U) {
        logger.error("failed to allocate mesh");
        return {-1U};
    }

    counts.meshes += 1;

    slice<byte> data_slice((byte *)&data, sizeof(MeshData));
    m_writeback_buffers.meshes.insert(idx * sizeof(MeshData), data);

    return {idx};
}

MaterialHandle RenderState::alloc_material(const MaterialData &data) {
    ZoneScoped;
    u32 idx = m_material_alloc.allocate();
    if (idx == -1U) {
        logger.error("failed to allocate material");
        return {-1U};
    }

    counts.materials += 1;

    m_writeback_buffers.materials.insert(idx * material_size, data);

    return {idx};
}

TextureHandle RenderState::alloc_texture(VkImageView view, VkSampler sampler) {
    ZoneScoped;
    u32 idx = m_texture_alloc.allocate();
    if (idx == -1U) {
        logger.error("failed to allocate texture");
        return {-1U};
    }

    counts.textures += 1;

    // m_global_ds.write_combined_image_sampler(0, idx, view, sampler);
    m_texture_writes.push_back(TextureWrite{idx, view, sampler});

    return {idx};
}

ObjectHandle RenderState::alloc_object() {
    ZoneScoped;
    u32 idx = m_object_alloc.allocate();
    if (idx == -1U) {
        logger.error("failed to allocate object");
        return {-1U};
    }

    m_object_data[idx] = ObjectData{
        .transform = {
            1, 0, 0,
            0, 1, 0,
            0, 0, 1,
            0, 0, 0,
        },
        .material = -1,
        .mesh = -1,
    };
    dirty_objects.insert({idx});

    counts.objects += 1;

    m_highest_object_id = std::max(m_highest_object_id, idx);
    return {idx};
}

void RenderState::update_object(ObjectHandle handle, const ObjectData &object) {
    m_object_data[handle.id] = object;
    dirty_objects.insert(handle);
}
const ObjectData &RenderState::object_data(ObjectHandle handle) {
    return m_object_data[handle.id];
}

void RenderState::update_global(const GlobalData &data) {
    m_writeback_buffers.global_data = data;
    m_writeback_buffers.global_data_dirty = true;
}

RenderState::FlushDependencies RenderState::flush(CommandBuffer &cmd) {
    ZoneScoped;

    WriteCache<ObjectData> object_changes;
    for (auto &handle : dirty_objects) {
        ObjectData &object = m_object_data[handle.id];
        object_changes.insert(handle.id * sizeof(ObjectData), object);
    }
    dirty_objects.clear();

    FlushDependencies deps;
    {
        TracyVkZone(cmd.tracy_ctx(), cmd.get(), "render-state-write");
        deps.vertices = m_vertex_buffer.multiwrite_with_barrier(cmd.get(), m_writeback_buffers.vertices.write_list());
        deps.indices = m_index_buffer.multiwrite_with_barrier(cmd.get(), m_writeback_buffers.indices.write_list());
        deps.materials = m_material_buffer.multiwrite_with_barrier(cmd.get(), m_writeback_buffers.materials.write_list());
        deps.meshes = m_mesh_buffer.multiwrite_with_barrier(cmd.get(), m_writeback_buffers.meshes.write_list());
        deps.objects = m_object_buffer.multiwrite_with_barrier(cmd.get(), object_changes.write_list());

        if (m_writeback_buffers.global_data_dirty) {
            slice<byte> global_data_slice((byte *)&m_writeback_buffers.global_data, sizeof(GlobalData));
            deps.global = m_global_buffer.write_with_barrier(cmd.get(), global_data_slice);
            m_writeback_buffers.global_data_dirty = false;
        }
    }

    m_writeback_buffers.vertices.clear();
    m_writeback_buffers.indices.clear();
    m_writeback_buffers.materials.clear();
    m_writeback_buffers.meshes.clear();

    m_texture_writes.clear();

    // update metrics
    metrics::gauge_u32("rend2.state.vertices", counts.vertices);
    metrics::gauge_u32("rend2.state.indices", counts.indices);
    metrics::gauge_u32("rend2.state.materials", counts.materials);
    metrics::gauge_u32("rend2.state.meshes", counts.meshes);
    metrics::gauge_u32("rend2.state.textures", counts.textures);
    metrics::gauge_u32("rend2.state.objects", counts.objects);

    return deps;
}

std::span<TextureWrite> RenderState::texture_writes() {
    return m_texture_writes;
}
