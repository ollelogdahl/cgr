#include "render.h"
#include "rend2/render_state.h"
#include "pipeline_layout_builder.h"
#include "descriptor_set_layout_builder.h"

#include <algorithm>

#include "cull_lod_spv.h"

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

VkFence create_fence(gpu_t &gpu, bool signal = true) {
    VkFence fence;
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = signal ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
    VK_CHECK(vkCreateFence(gpu.device, &fence_info, nullptr, &fence));
    return fence;
}

Renderer::Renderer(gpu_t &gpu) : m_gpu(&gpu), m_state(gpu, config), cull_lod_compute({
    CommandBuffer(gpu, gpu.compute_queue, gpu.compute_command_pool, "cull_lod_compute")
}) {
    m_draw_buffer = GpuBuffer(gpu, max_draws * sizeof(DrawCommand) + 1 * sizeof(u32),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    m_pipeline_layout = PipelineLayoutBuilder()
        .add_descriptor_set(m_state.global_descriptor_set().layout())
        .build(gpu);

    cull_lod_compute.fence = create_fence(gpu);

    // setup the annoying compute shader
    VkDescriptorPool pool;
    {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = array_size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = 1;

        VK_CHECK(vkCreateDescriptorPool(gpu.device, &pool_info, nullptr, &pool));
    }

    {
        //      binding 0: object buffer
        //      binding 1: draw buffer
        //      binding 2: mesh buffer
        VkDescriptorSetLayout ds_layout = DescriptorSetLayoutBuilder()
            .add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .add_binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build(gpu);

        cull_lod_compute.descriptor_set.init(gpu, pool, ds_layout);

        cull_lod_compute.descriptor_set.write_storage_buffer(0, 0, m_state.object_buffer(), 0, VK_WHOLE_SIZE);
        cull_lod_compute.descriptor_set.write_storage_buffer(1, 0, m_draw_buffer.get(), 0, VK_WHOLE_SIZE);
        cull_lod_compute.descriptor_set.write_storage_buffer(2, 0, m_state.mesh_buffer(), 0, VK_WHOLE_SIZE);
        cull_lod_compute.descriptor_set.flush(gpu);

        cull_lod_compute.pipeline_layout = PipelineLayoutBuilder()
            .add_descriptor_set(ds_layout)
            .build(gpu);
    }

    // make the funking pipeline
    {
        auto shader_spv = cull_lod_spv;
    }
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
    // @todo: flush currently creates barriers. This is no good; we do not know how
    // the buffer will be used!!!
    // Instead, we should make the barrier here.

    // figure out the different shader batches.
    // for now, only support one shader.
    VkBuffer vertex_buffers[] = {m_state.vertex_buffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(frame.cmds, 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(frame.cmds, m_state.index_buffer(), 0, VK_INDEX_TYPE_UINT32);

    // bind the global descriptor set
    VkDescriptorSet descriptor_sets[] = { m_state.global_descriptor_set().get() };
    vkCmdBindDescriptorSets(frame.cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, 0,
        1, descriptor_sets, 0, nullptr);

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
    {
        ZoneScopedN("cull-lod-compute");
        // we would really like to do this on another frame i guess??
        {
            ZoneScopedN("wait-ready");
            VK_CHECK(vkWaitForFences(m_gpu->device, 1, &cull_lod_compute.fence, VK_TRUE, UINT64_MAX));
            VK_CHECK(vkResetFences(m_gpu->device, 1, &cull_lod_compute.fence));
        }

        auto &cmd = cull_lod_compute.cmd;
        cmd.reset_begin();
        TracyVkCollect(cmd.tracy_ctx(), cmd.get());

        {
            TracyVkZone(cmd.tracy_ctx(), cmd.get(), "cull_lod_compute");


        }

        cmd.end();

        {
            // submit the command buffer
            VkCommandBuffer buffers[] = { cmd.get() };

            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = array_size(buffers);
            submit_info.pCommandBuffers = buffers;
            vkQueueSubmit(m_gpu->compute_queue, 1, &submit_info, cull_lod_compute.fence);
        }
    }
}

bool operator==(const LoadShaderProperties &lhs, const LoadShaderProperties &rhs) {
    return lhs.glsl_vert_path == rhs.glsl_vert_path && lhs.glsl_frag_path == rhs.glsl_frag_path;
}

std::size_t std::hash<LoadShaderProperties>::operator()(const LoadShaderProperties &props) const {
    return std::hash<const char *>()(props.glsl_vert_path) ^ std::hash<const char *>()(props.glsl_frag_path);
}
