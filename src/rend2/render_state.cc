#include "render_state.h"
#include "rend2/render_handles.h"
#include "descriptor_set_layout_builder.h"

const u32 vertex_size = 9 * sizeof(f32);
const u32 index_size = sizeof(u32);
const u32 material_size = 32;
const u32 transform_size = 12;
const u32 global_size = 64;

RenderState::RenderState(gpu_t &gpu, const RenderStateConfig &config)
:
    m_gpu(&gpu),
    m_object_buffer(gpu, config.max_objects * sizeof(ObjectData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    m_object_data(new ObjectData[config.max_objects]),
    m_vertex_buffer(gpu, config.max_vertices * vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
    m_index_buffer(gpu, config.max_indices * index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
    m_material_buffer(gpu, config.max_materials * material_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),
    m_mesh_buffer(gpu, config.max_meshes * transform_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),
    m_global_buffer(gpu, global_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),
    m_vertex_alloc(config.max_vertices),
    m_index_alloc(config.max_indices),
    m_material_alloc(config.max_materials),
    m_mesh_alloc(config.max_meshes),
    m_texture_alloc(config.max_textures),
    m_object_alloc(config.max_objects) {

    // create the global descriptor set
    {
        // create the pool
        {
            VkDescriptorPoolSize pool_sizes[] = {
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
                { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, config.max_textures },
            };

            VkDescriptorPoolCreateInfo pool_info = {};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.poolSizeCount = array_size(pool_sizes);
            pool_info.pPoolSizes = pool_sizes;
            pool_info.maxSets = 1;

            VK_CHECK(vkCreateDescriptorPool(gpu.device, &pool_info, nullptr, &m_descriptor_pool));
        }

        // create the layout
        VkDescriptorSetLayout layout = DescriptorSetLayoutBuilder()
            .add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .add_binding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
            .add_binding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .add_variable_binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
                VK_SHADER_STAGE_FRAGMENT_BIT, config.max_textures)
            .build(gpu);

        m_global_ds.init(gpu, m_descriptor_pool, layout);
    }

    m_global_ds.write_buffer(0, 0, m_global_buffer.get(), 0, VK_WHOLE_SIZE);
    m_global_ds.write_buffer(1, 0, m_mesh_buffer.get(), 0, VK_WHOLE_SIZE);
    m_global_ds.write_buffer(2, 0, m_material_buffer.get(), 0, VK_WHOLE_SIZE);
    m_global_ds.flush(*m_gpu);
}

VertexDataHandle RenderState::alloc_vertices(slice<byte> vertices) {
    u32 num_vertices = vertices.len / vertex_size;
    u32 start = m_vertex_alloc.allocate(num_vertices);
    if (start == -1U) {
        return { -1U, 0 };
    }

    m_writeback_buffers.vertices.insert({start * vertex_size, {vertices.begin(), vertices.end()}});

    return { start, num_vertices };
}

IndexDataHandle RenderState::alloc_indices(std::vector<u32> &&indices) {
    u32 start = m_index_alloc.allocate(indices.size());
    if (start == -1U) {
        return { -1U, 0 };
    }

    m_writeback_buffers.indices.insert({start, std::move(indices)});

    return { start, (u32)indices.size() };
}

MeshHandle RenderState::alloc_mesh(const MeshData &data) {
    u32 idx = m_mesh_alloc.allocate();
    if (idx == -1U) {
        return {-1U};
    }

    slice<byte> data_slice((byte *)&data, sizeof(MeshData));
    m_writeback_buffers.meshes.insert(idx, data);

    return {idx};
}

MaterialHandle RenderState::alloc_material(const MaterialData &data) {
    u32 idx = m_material_alloc.allocate();
    if (idx == -1U) {
        return {-1U};
    }

    m_writeback_buffers.materials.insert(idx * material_size, data);

    return {idx};
}

TextureHandle RenderState::alloc_texture(VkImageView view, VkSampler sampler) {
    u32 idx = m_texture_alloc.allocate();
    if (idx == -1U) {
        return {-1U};
    }

    m_global_ds.write_combined_image_sampler(0, idx, view, sampler);

    return {idx};
}

ObjectHandle RenderState::alloc_object() {
    u32 idx = m_object_alloc.allocate();
    if (idx == -1U) {
        return {-1U};
    }
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

void RenderState::flush(VkCommandBuffer cmd) {
    m_vertex_buffer.multiwrite_with_barrier(cmd, m_writeback_buffers.vertices.write_list());
    m_index_buffer.multiwrite_with_barrier(cmd, m_writeback_buffers.indices.write_list());
    m_material_buffer.multiwrite_with_barrier(cmd, m_writeback_buffers.materials.write_list());
    m_mesh_buffer.multiwrite_with_barrier(cmd, m_writeback_buffers.meshes.write_list());

    {
        WriteCache<ObjectData> object_changes;
        for (auto &handle : dirty_objects) {
            ObjectData &object = m_object_data[handle.id];
            object_changes.insert(handle.id, object);
        }
        m_object_buffer.multiwrite_with_barrier(cmd, object_changes.write_list());
    }

    if (m_writeback_buffers.global_data_dirty) {
        slice<byte> global_data_slice((byte *)&m_writeback_buffers.global_data, sizeof(GlobalData));
        m_global_buffer.write_with_barrier(cmd, global_data_slice);
        m_writeback_buffers.global_data_dirty = false;
    }

    m_global_ds.flush(*m_gpu);

    dirty_objects.clear();
    m_writeback_buffers.vertices.clear();
    m_writeback_buffers.indices.clear();
    m_writeback_buffers.materials.clear();
    m_writeback_buffers.meshes.clear();
}
