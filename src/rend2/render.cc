#include "render.h"
#include "gpu.h"
#include "metrics.h"
#include "model.h"
#include "rend2/buffer.h"
#include "rend2/render_state.h"
#include "pipeline_layout_builder.h"
#include "descriptor_set_layout_builder.h"

#include <algorithm>


#include "rend2/shader_compiler.h"
#include "rend2/vku.h"
#include "tracy/Tracy.hpp"

class TrackedResourceID {
public:
    inline constexpr TrackedResourceID(const char *s, size_t n) {
        const u64 fnv1a_offset_basis = 0xcbf29ce484222325;
        const u64 fnv1a_prime = 0x00000100000001b3;

        m_hash = fnv1a_offset_basis;

        for (size_t i = 0; i < n; ++i) {
            m_hash ^= (u64)s[i];
            m_hash *= fnv1a_prime;
        }
    }
private:
    u64 m_hash;
};

inline constexpr TrackedResourceID operator ""_tr(const char *s, size_t n) {
    return TrackedResourceID(s, n);
}

class ResourceTracker {
public:
    struct Range {
        u64 start;
        u64 size;
    };

    void compute_ssbo_write(VkBuffer buffer, Range range);
    void compute_ssbo_read(VkBuffer buffer, Range range);
private:
};

class ImageDependency {
public:
    ImageDependency() = default;
    ImageDependency(VkImage image, VkPipelineStageFlags2 src_stage,
        VkAccessFlags2 src_access /* @todo: layouts */) {
        add(src_stage, src_access, image);
    }

    void add(VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkImage image) {
        barriers.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = src_stage,
            .srcAccessMask = src_access,
            .dstStageMask = 0,
            .dstAccessMask = 0,
            .image = image,
        });
    }

    void join(ImageDependency &other) {
        barriers.insert(barriers.end(), other.barriers.begin(), other.barriers.end());
    }

    void pipeline_barrier(VkCommandBuffer cmd, VkPipelineStageFlags2 dst_stage,
        VkAccessFlags2 dst_access) {
        for (auto &barrier : barriers) {
            barrier.dstStageMask = dst_stage;
            barrier.dstAccessMask = dst_access;
        }

        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = barriers.size();
        dependency.pImageMemoryBarriers = barriers.data();

        vkCmdPipelineBarrier2(cmd, &dependency);
    }
private:
    std::vector<VkImageMemoryBarrier2> barriers;
};

static const RenderStorageConfig config = {
    .max_objects = 30 * 1024,
    .max_vertices = 1 * 1024 * 1024,
    .max_indices = 1 * 1024 * 1024,
    .max_meshes = 1024,
    .max_materials = 20 * 1024,
    .max_textures = 1024,
    .max_lights = 1024 * 1024,
};
static const u32 max_draws = config.max_objects;

Renderer::Renderer(gpu_t &gpu, RenderStorage &storage,  ShaderCompiler &sc)
:   m_gpu(&gpu),
    m_storage(storage),
    m_state(gpu, storage, sc),
    m_shader_compiler(sc),
    m_draw_buffer(gpu, max_draws * sizeof(DrawCommand) + 1 * sizeof(u32),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    m_forward_pass(gpu, sc, m_storage, m_draw_buffer),
    m_shadow_pass(gpu, sc, m_storage, m_draw_buffer),
    m_cluster_shading(gpu, sc, ClusterConfig{
        .grid_x = 16,
        .grid_y = 8,
        .grid_z = 22,
        .light_buffer = m_storage.light_buffer(),
    }) {

    set_object_name(gpu, VK_OBJECT_TYPE_BUFFER, m_draw_buffer.get(), "draw-buffer");

    m_state.set_forward_pipeline_layout(m_forward_pass.pipeline_layout());

    m_storage.render_descriptor_set().write_storage_buffer(4, 0, m_cluster_shading.cluster_buffer().get(), 0, VK_WHOLE_SIZE);
    m_storage.render_descriptor_set().write_storage_buffer(5, 0, m_cluster_shading.cluster_item_buffer().get(), 0, VK_WHOLE_SIZE);
    m_storage.render_descriptor_set().flush(gpu);
}

void Renderer::render(gpu_t::frame_t &frame, const View &view) {
    ZoneScoped;

    WriteDependency pre_cluster_build_dependencies;
    WriteDependency pre_cluster_assign_dependencies;
    WriteDependency pre_forward_dependencies;

    // wait for last frame to read mutable data
    pre_cluster_build_dependencies.add(
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT,
        m_cluster_shading.cluster_buffer().get());
    pre_cluster_assign_dependencies.add(
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT,
        m_cluster_shading.cluster_item_buffer().get());

    m_storage.update_global({
        .view = view.view,
        .proj = view.projection,
        .view_pos = view.position,
    });

    {
        pre_cluster_build_dependencies.pipeline_barrier(frame.cmd.get(),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);

        // @todo: only do this when projection changes.
        // @todo: view could contain m4fbi instead?
        m4f inv_proj = m4f::inverse(view.projection);
        m_cluster_shading.rebuild_clusters(frame.cmd, view.znear, view.zfar, inv_proj);

        pre_cluster_assign_dependencies.add(
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT,
            m_cluster_shading.cluster_buffer().get());

        pre_forward_dependencies.add(
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT,
            m_cluster_shading.cluster_buffer().get());
    }

    m_state.finalize_before_render(frame.cmd);
    auto state_dependencies = m_storage.flush(frame.cmd);

    pre_cluster_assign_dependencies.join(state_dependencies.objects);

    pre_forward_dependencies.join(state_dependencies.objects);
    pre_forward_dependencies.join(state_dependencies.meshes);

    {
        // assign items to clusters.
        pre_cluster_assign_dependencies.pipeline_barrier(frame.cmd.get(),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);

        m4f vp = view.view * view.projection;
        m_cluster_shading.assign_items(frame.cmd, vp, view.view);

        pre_forward_dependencies.add(
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT,
            m_cluster_shading.cluster_item_buffer().get());
    }

    /*
    {
        pre_forward_dependencies.pipeline_barrier(frame.cmd.get(),
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    }

    {
        RenderTarget target = {
            .color_view = m_gpu->swapchain.image_views[frame.image_idx],
            .depth_view = m_gpu->depth_image.view,
            .extent = m_gpu->swapchain.extent,
        };

        auto store_batches = m_storage.object_batches();
        std::vector<IndirectBatch2> batches(store_batches.size());
        for (size_t i = 0; i < store_batches.size(); i++) {
            auto &batch = store_batches[i];
            batches[i] = {
                .pipeline = m_storage.shaders()[batch.id].render_pipeline,
                .buffer_offset = batch.start_index,
                .count = batch.count,
            };
        }

        m_forward_pass.record(frame.cmd, target, view, batches);
    }
    */
}

bool operator==(const LoadShaderProperties &lhs, const LoadShaderProperties &rhs) {
    return lhs.glsl_vert_path == rhs.glsl_vert_path && lhs.glsl_frag_path == rhs.glsl_frag_path;
}

std::size_t std::hash<LoadShaderProperties>::operator()(const LoadShaderProperties &props) const {
    return std::hash<const char *>()(props.glsl_vert_path) ^ std::hash<const char *>()(props.glsl_frag_path);
}

bool operator==(const LoadTextureProperties &lhs, const LoadTextureProperties &rhs) {
    return lhs.path == rhs.path && lhs.type == rhs.type;
}

std::size_t std::hash<LoadTextureProperties>::operator()(const LoadTextureProperties &props) const {
    return std::hash<const char *>()(props.path) ^ std::hash<u32>()(static_cast<u32>(props.type));
}
