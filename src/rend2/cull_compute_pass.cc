#include "cull_compute_pass.h"

#include "metrics.h"
#include "rend2/buffer.h"
#include "rend2/command_buffer.h"
#include "rend2/descriptor_set_layout_builder.h"
#include "rend2/pipeline_layout_builder.h"
#include "rend2/shader_compiler.h"
#include "rend2/vku.h"
#include <vulkan/vulkan_core.h>

#include <tracy/Tracy.hpp>

struct CullStatistics {
    u32 draw_count;
    u32 draw_count_lod[4];
};

CullComputePass::CullComputePass(gpu_t &gpu, ShaderCompiler &sc)
: m_gpu(&gpu),
    m_cull_buffer(gpu, sizeof(CullData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    m_stats_buffer(gpu, sizeof(CullStatistics), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferType::Readback)
{
    // @todo: remove this?
    VkDescriptorPool descriptor_pool;
    {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 },
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = array_size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = 1;

        VK_CHECK(vkCreateDescriptorPool(gpu.device, &pool_info, nullptr, &descriptor_pool));
    }

    //      binding 0: object buffer    (IN)
    //      binding 1: draw buffer      (IN-OUT)
    //      binding 2: mesh buffer      (IN)
    //      binding 3: cull buffer      (IN)
    //      binding 4: stats buffer     (OUT)
    VkDescriptorSetLayout ds_layout = DescriptorSetLayoutBuilder()
        .add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .add_binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .add_binding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .add_binding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .build(gpu);

    m_descriptor_set.init(gpu, descriptor_pool, ds_layout);
    m_descriptor_set.write_storage_buffer(3, 0, m_cull_buffer.get(), 0, VK_WHOLE_SIZE);
    m_descriptor_set.write_storage_buffer(4, 0, m_stats_buffer.get(), 0, VK_WHOLE_SIZE);

    m_pipeline_layout = PipelineLayoutBuilder()
        .add_descriptor_set(ds_layout)
        .build(gpu);

    // @todo: move this.
    Shader shader = Shader({
        sc.compile("shaders/cull-lod.comp")
    });
    m_pipeline = create_compute_pipeline(gpu, m_pipeline_layout, shader);
}

void CullComputePass::bind_buffers(const GpuBuffer &object_buffer, const GpuBuffer &draw_buffer, const GpuBuffer &mesh_buffer) {
    m_descriptor_set.write_storage_buffer(0, 0, object_buffer.get(), 0, VK_WHOLE_SIZE);
    m_descriptor_set.write_storage_buffer(1, 0, draw_buffer.get(), 0, VK_WHOLE_SIZE);
    m_descriptor_set.write_storage_buffer(2, 0, mesh_buffer.get(), 0, VK_WHOLE_SIZE);
    m_descriptor_set.flush(*m_gpu);

    m_draw_buffer = &draw_buffer;
}

void CullComputePass::update_view(const v3f &eye, const m4f &vp) {
    m_cull_data.eye = eye;

    // extract planes from view-projection matrix
    m4f::extract_planes(vp, m_cull_data.planes);
    m_cull_data_dirty = true;
}

const char *draw_count_lod_metric_names[] = {
    "rend2.cull.draw_count_lod[0]",
    "rend2.cull.draw_count_lod[1]",
    "rend2.cull.draw_count_lod[2]",
    "rend2.cull.draw_count_lod[3]",
};

void CullComputePass::record(CommandBuffer &cmd, u32 object_count) {
    ZoneScoped;
    TracyVkZone(cmd.tracy_ctx(), cmd.get(), "cull_lod_compute");

    // read statistics from last frame
    {
        CullStatistics *stats = m_stats_buffer.read<CullStatistics>(0);
        metrics::gauge_u32("rend2.cull.draw_count", stats->draw_count);
        for (u32 i = 0; i < 4; i++) {
            metrics::gauge_u32(draw_count_lod_metric_names[i], stats->draw_count_lod[i]);
        }

        memset(stats, 0, sizeof(CullStatistics));
    }

    // clear the draw buffer
    {
        TracyVkZone(cmd.tracy_ctx(), cmd.get(), "wait_last_frame");

        VkMemoryBarrier2 memory_barrier{};
        memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        memory_barrier.srcAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        memory_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;

        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &memory_barrier;

        vkCmdPipelineBarrier2(cmd.get(), &dependency);
    }

    WriteDependency dependencies;
    {
        TracyVkZone(cmd.tracy_ctx(), cmd.get(), "clear");
        vkCmdFillBuffer(cmd.get(), m_draw_buffer->get(), 0, VK_WHOLE_SIZE, 0);
        dependencies.add(
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            m_draw_buffer->get());
    }

    // write new cull data
    if (m_cull_data_dirty) {
        auto b = m_cull_buffer.write_with_barrier(cmd.get(), slice<byte>((byte *)&m_cull_data, sizeof(CullData)));
        dependencies.join(b);
        m_cull_data_dirty = false;
    }

    {
        TracyVkZone(cmd.tracy_ctx(), cmd.get(), "wait_cull_pass");
        dependencies.pipeline_barrier(cmd.get(),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    }

    VkDescriptorSet descriptor_sets[] = { m_descriptor_set.get() };

    {
        TracyVkZone(cmd.tracy_ctx(), cmd.get(), "dispatch");

        vkCmdBindPipeline(cmd.get(), VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdBindDescriptorSets(cmd.get(), VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layout, 0,
            array_size(descriptor_sets), descriptor_sets, 0, nullptr);

        u32 count = (object_count + m_workgroup_size - 1) / m_workgroup_size;
        vkCmdDispatch(cmd.get(), count, 1, 1);
    }

    metrics::gauge_u32("rend2.cull.objects", object_count);
}
