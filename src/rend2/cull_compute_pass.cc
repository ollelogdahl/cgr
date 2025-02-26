#include "cull_compute_pass.h"

#include "cull_lod_spv.h"
#include "rend2/command_buffer.h"
#include "rend2/descriptor_set_layout_builder.h"
#include "rend2/pipeline_layout_builder.h"
#include "rend2/shader_compiler.h"
#include "rend2/vku.h"
#include <vulkan/vulkan_core.h>

#include <tracy/Tracy.hpp>

CullComputePass::CullComputePass(gpu_t &gpu)
: m_gpu(&gpu) {
    // @todo: remove this?
    VkDescriptorPool descriptor_pool;
    {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = array_size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = 1;

        VK_CHECK(vkCreateDescriptorPool(gpu.device, &pool_info, nullptr, &descriptor_pool));
    }

    //      binding 0: object buffer
    //      binding 1: draw buffer
    //      binding 2: mesh buffer
    VkDescriptorSetLayout ds_layout = DescriptorSetLayoutBuilder()
        .add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .add_binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .build(gpu);

    m_descriptor_set.init(gpu, descriptor_pool, ds_layout);

    m_pipeline_layout = PipelineLayoutBuilder()
        .add_descriptor_set(ds_layout)
        .build(gpu);

    // @todo: move this.
    ShaderCompiler compiler(gpu, "glslc");
    Shader shader = Shader({
        compiler.compile("shaders/cull-lod.comp")
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

void CullComputePass::record(CommandBuffer &cmd, u32 object_count) {
    ZoneScoped;
    TracyVkZone(cmd.tracy_ctx(), cmd.get(), "cull_lod_compute");

    vkCmdFillBuffer(cmd.get(), m_draw_buffer->get(), 0, sizeof(u32), 0);

    // barrier before starting dispatch

    VkBufferMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    barrier.buffer = m_draw_buffer->get();
    barrier.size = sizeof(u32);

    VkDependencyInfo dep_info = {};
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.pNext = nullptr;
    dep_info.bufferMemoryBarrierCount = 1;
    dep_info.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd.get(), &dep_info);


    VkDescriptorSet descriptor_sets[] = { m_descriptor_set.get() };

    vkCmdBindPipeline(cmd.get(), VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd.get(), VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layout, 0,
        array_size(descriptor_sets), descriptor_sets, 0, nullptr);

    u32 count = 1 + (object_count / 16);
    vkCmdDispatch(cmd.get(), count, 1, 1);
}
