#include "render_state.h"
#include "gpu.h"
#include "metrics.h"
#include "rend2/render_handles.h"
#include "descriptor_set_layout_builder.h"
#include "rend2/vku.h"

#include <map>
#include <algorithm>

#include "log.h"

#include <tracy/Tracy.hpp>
#include <vulkan/vulkan_core.h>

const u32 vertex_size = 9 * sizeof(f32);
const u32 index_size = sizeof(u32);

logger_t logger = logger_t("renderstorage");

RenderStorage::RenderStorage(gpu_t &gpu, const RenderStorageConfig &config)
:
    // @todo: make some into storage buffers instead
    m_gpu(&gpu),
    m_object_buffer(gpu, config.max_objects * sizeof(ObjectData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    m_object_data(new ObjectData[config.max_objects]),
    m_vertex_buffer(gpu, config.max_vertices * vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
    m_index_buffer(gpu, config.max_indices * index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
    m_material_buffer(gpu, config.max_materials * sizeof(MaterialData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    m_mesh_buffer(gpu, config.max_meshes * sizeof(MeshData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    m_global_buffer(gpu, sizeof(GlobalData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    m_textures(new TextureSlot[config.max_textures]),
    m_vertex_alloc(config.max_vertices),
    m_index_alloc(config.max_indices),
    m_material_alloc(config.max_materials),
    m_mesh_alloc(config.max_meshes),
    m_texture_alloc(config.max_textures),
    m_object_alloc(config.max_objects),
    m_lights(
        GpuBuffer(gpu, config.max_lights * sizeof(LightData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
        new LightData[config.max_lights],
        SlotAllocator(config.max_lights)
    ) {

    u64 gpu_total_size_bytes = m_object_buffer.size() + m_vertex_buffer.size() + m_index_buffer.size() +
        m_material_buffer.size() + m_mesh_buffer.size() + m_global_buffer.size();
    metrics::gauge_u64("rend2.state.gpu_size", gpu_total_size_bytes, "b");

    // write default values to the object buffer
    // @todo: make some default batch with default shader.
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
            .batch = -1U,
        };


        for (u32 i = 0; i < config.max_objects; ++i) {
            m_object_data[i] = default_object;
        }

        m_object_buffer.write(slice<byte>((byte *)m_object_data, config.max_objects * sizeof(ObjectData)));
    }

    // create the descriptor pool & set
    {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, config.max_textures },
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = array_size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = 1;

        VkDescriptorPool descriptor_pool;
        VK_CHECK(vkCreateDescriptorPool(gpu.device, &pool_info, nullptr, &descriptor_pool));

        auto ds_builder = DescriptorSetLayoutBuilder()
            .add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
            .add_binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .add_binding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .add_binding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .add_binding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .add_variable_binding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, config.max_textures);
        ds_builder.create_set(gpu, descriptor_pool, m_render_descriptor_set);

        m_render_descriptor_set.write_storage_buffer(0, 0, m_global_buffer.get(), 0, VK_WHOLE_SIZE);
        m_render_descriptor_set.write_storage_buffer(1, 0, m_object_buffer.get(), 0, VK_WHOLE_SIZE);
        m_render_descriptor_set.write_storage_buffer(2, 0, m_material_buffer.get(), 0, VK_WHOLE_SIZE);
        m_render_descriptor_set.write_storage_buffer(3, 0, m_lights.buffer.get(), 0, VK_WHOLE_SIZE);
        m_render_descriptor_set.flush(gpu);
    }
}

VertexDataHandle RenderStorage::alloc_vertices(slice<byte> vertices) {
    ZoneScoped;
    u32 num_vertices = vertices.len / vertex_size;
    u32 start = m_vertex_alloc.allocate(num_vertices);
    if (start == -1U) {
        logger.error("failed to allocate {} vertices", num_vertices);
        return { -1U, 0 };
    }

    counts.vertices += num_vertices;

    m_writes.vertices.insert({start * vertex_size, {vertices.begin(), vertices.end()}});

    return { start, num_vertices };
}

IndexDataHandle RenderStorage::alloc_indices(std::vector<u32> &&indices) {
    ZoneScoped;
    u32 start = m_index_alloc.allocate(indices.size());
    if (start == -1U) {
        logger.error("failed to allocate {} indices", indices.size());
        return { -1U, 0 };
    }

    counts.indices += indices.size();

    u32 written = indices.size();
    m_writes.indices.insert({start * (u32)sizeof(u32), std::move(indices)});

    return { start, written };
}

MeshHandle RenderStorage::alloc_mesh(const MeshData &data) {
    ZoneScoped;
    u32 idx = m_mesh_alloc.allocate();
    if (idx == -1U) {
        logger.error("failed to allocate mesh");
        return {-1U};
    }

    counts.meshes += 1;

    slice<byte> data_slice((byte *)&data, sizeof(MeshData));
    m_writes.meshes.insert(idx * sizeof(MeshData), data);

    return {idx};
}

MaterialHandle RenderStorage::alloc_material(const MaterialData &data) {
    ZoneScoped;
    u32 idx = m_material_alloc.allocate();
    if (idx == -1U) {
        logger.error("failed to allocate material");
        return {-1U};
    }

    counts.materials += 1;

    m_writes.materials.insert(idx * sizeof(MaterialData), data);

    return {idx};
}

TextureHandle RenderStorage::alloc_texture(VkImage image, VkImageView view, VkSampler sampler) {
    ZoneScoped;
    u32 idx = m_texture_alloc.allocate();
    if (idx == -1U) {
        logger.error("failed to allocate texture");
        return {-1U};
    }

    counts.textures += 1;

    m_render_descriptor_set.write_combined_image_sampler(6, idx, image, view, sampler);
    m_textures[idx] = {image, view, sampler};

    return {idx};
}

ObjectHandle RenderStorage::alloc_object() {
    ZoneScoped;
    u32 idx = m_object_alloc.allocate();
    if (idx == -1U) {
        logger.error("failed to allocate object");
        return {-1U};
    }

    ObjectData default_object = {
        .transform = {
            1, 0, 0,
            0, 1, 0,
            0, 0, 1,
            0, 0, 0,
        },
        .material = -1,
        .mesh = -1,
        .batch = -1U,
    };

    // @todo: we should be able to create the object with some data.
    m_object_data[idx] = default_object;
    m_object_to_idx_map.insert({idx, idx});
    dirty_objects.insert({idx});

    counts.objects += 1;

    m_highest_object_id = std::max(m_highest_object_id, idx);

    return {idx};
}

void RenderStorage::update_object(ObjectHandle handle, const ObjectData &object) {
    u32 idx = m_object_to_idx_map[handle.id];
    BatchId new_batch = object.batch;
    BatchId old_batch = m_object_data[idx].batch;

    if (new_batch == old_batch) {
        m_object_data[idx] = object;
        dirty_objects.insert(handle);
        return;
    }

    // this could be done faster.
    auto batch_it = std::find_if(m_object_batches.begin(), m_object_batches.end(), [&](const Batch &b) {
        return b.id == new_batch;
    });

    // if there doesn't exist a batch, create one!
    if (batch_it == m_object_batches.end()) {
        m_object_batches.push_back(Batch{
            .id = new_batch,
            .start_index = idx,
            .count = 1,
        });

        return;
    }

    // there exist another batch. We need to reconcile the two.
    // This is tricky! For now, objects are allocated and never deallocated.
    // This means that the new object is always above the old batch.
    //
    // case 0:
    //           X     N
    // [0][0][0][1][1][0]
    //      -> swap N with first 1 (X).
    //
    // case 1:
    //           Y     X     N
    // [0][0][0][1][1][2][2][0]
    //      -> swap N with first 2 (X). Then swap X with first 1 (Y).
    //
    // we simply 'bubble down'.

    // figure out the target batch index.
    u32 target_idx = batch_it - m_object_batches.begin();

    u32 cursor = idx;
    for (u32 i = m_object_batches.size() - 1; i > target_idx; --i) {
        auto &batch = m_object_batches[i];
        std::swap(m_object_data[cursor], m_object_data[batch.start_index]);
        cursor = batch.start_index;

        batch.start_index += 1;

        dirty_objects.insert({batch.start_index});
        dirty_objects.insert({cursor});
    }

    // we are now one element above the target batch.
    // we can now update the target batch.
    batch_it->count += 1;
}

const ObjectData &RenderStorage::object_data(ObjectHandle handle) {
    u32 idx = m_object_to_idx_map[handle.id];
    return m_object_data[idx];
}

ShaderHandle RenderStorage::store_shader(const ShaderInfo &info) {
    ShaderHandle handle = {(u32)m_shaders.size()};
    m_shaders.push_back(info);
    return handle;
}

LightHandle RenderStorage::alloc_light(const LightData &data) {
    ZoneScoped;
    u32 idx = m_lights.allocator.allocate();
    if (idx == -1U) {
        logger.error("failed to allocate light");
        return {-1U};
    }
    m_lights.highest_id = std::max(m_lights.highest_id, idx);

    counts.lights += 1;

    m_lights.dirty.insert({idx});
    m_lights.data[idx] = data;

    return {idx};
}

void RenderStorage::update_light(LightHandle handle, const LightData &data) {
    m_lights.dirty.insert(handle);
    m_lights.data[handle.id] = data;
}

void RenderStorage::update_material(MaterialHandle handle, const MaterialData &data) {
    m_writes.materials.insert(handle.id * sizeof(MaterialData), data);
}

void RenderStorage::update_global(const GlobalData &data) {
    m_writes.global_data = data;
    m_writes.global_data_dirty = true;
}

RenderStorage::FlushDependencies RenderStorage::flush(CommandBuffer &cmd) {
    ZoneScoped;

    WriteCache<ObjectData> object_changes;
    for (auto &handle : dirty_objects) {
        ObjectData &object = m_object_data[handle.id];
        object_changes.insert(handle.id * sizeof(ObjectData), object);
    }
    dirty_objects.clear();

    WriteCache<LightData> light_changes;
    for (auto &handle : m_lights.dirty) {
        LightData &light = m_lights.data[handle.id];
        light_changes.insert(16 + handle.id * sizeof(LightData), light);
    }
    m_lights.dirty.clear();

    FlushDependencies deps;

    deps.textures = m_render_descriptor_set.flush(*m_gpu);

    {
        TracyVkZone(cmd.tracy_ctx(), cmd.get(), "render-state-write");

        if (!m_writes.vertices.empty()) {
            logger.info("flushing {} writes of vertices (tot: {} bytes)", m_writes.vertices.num_writes(), m_writes.vertices.total_bytes());
            deps.vertices = m_vertex_buffer.multiwrite_with_barrier(cmd.get(), m_writes.vertices.write_list());
            m_writes.vertices.clear();
        }
        if (!m_writes.indices.empty()) {
            logger.info("flushing {} writes of indices (tot: {} bytes)", m_writes.indices.num_writes(), m_writes.indices.total_bytes());
            deps.indices = m_index_buffer.multiwrite_with_barrier(cmd.get(), m_writes.indices.write_list());
            m_writes.indices.clear();
        }
        if (!m_writes.materials.empty()) {
            deps.materials = m_material_buffer.multiwrite_with_barrier(cmd.get(), m_writes.materials.write_list());
            m_writes.materials.clear();
        }
        if (!m_writes.meshes.empty()) {
            deps.meshes = m_mesh_buffer.multiwrite_with_barrier(cmd.get(), m_writes.meshes.write_list());
            m_writes.meshes.clear();
        }
        if (!object_changes.empty()) {
            deps.objects = m_object_buffer.multiwrite_with_barrier(cmd.get(), object_changes.write_list());
        }
        if (!light_changes.empty()) {
            deps.lights = m_lights.buffer.multiwrite_with_barrier(cmd.get(), light_changes.write_list());
            auto size_dep = m_lights.buffer.write_with_barrier(cmd.get(), slice<byte>((byte *)&m_lights.highest_id, sizeof(u32)), 0);
            deps.lights.join(size_dep);
        }

        if (m_writes.global_data_dirty) {
            slice<byte> global_data_slice((byte *)&m_writes.global_data, sizeof(GlobalData));
            deps.global = m_global_buffer.write_with_barrier(cmd.get(), global_data_slice);
            m_writes.global_data_dirty = false;
        }
    }

    // update metrics
    metrics::gauge_u32("rend2.state.vertices", counts.vertices);
    metrics::gauge_u32("rend2.state.indices", counts.indices);
    metrics::gauge_u32("rend2.state.materials", counts.materials);
    metrics::gauge_u32("rend2.state.meshes", counts.meshes);
    metrics::gauge_u32("rend2.state.textures", counts.textures);
    metrics::gauge_u32("rend2.state.objects", counts.objects);
    metrics::gauge_u32("rend2.state.lights", counts.lights);

    return deps;
}
