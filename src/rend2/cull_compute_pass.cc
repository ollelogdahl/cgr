#include "cull_compute_pass.h"

#include "cull_lod_spv.h"
#include "rend2/command_buffer.h"
#include "rend2/descriptor_set_layout_builder.h"
#include "rend2/pipeline_layout_builder.h"
#include "rend2/vku.h"
#include <vulkan/vulkan_core.h>

#include <tracy/Tracy.hpp>

CullComputePass::CullComputePass(gpu_t &gpu)
: m_gpu(&gpu), m_cmd(gpu, gpu.compute_queue, gpu.compute_command_pool, "cull_lod_compute") {
    m_fence = create_fence(gpu);

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

    // make the funking pipeline
    {
        VkShaderModule shader_module;
        {
            VkShaderModuleCreateInfo create_info = {};
            create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            create_info.codeSize = sizeof(cull_lod_spv);
            create_info.pCode = (u32 *)cull_lod_spv;

            VK_CHECK(vkCreateShaderModule(gpu.device, &create_info, nullptr, &shader_module));
        }

        VkPipelineShaderStageCreateInfo shader_stage_info = {};
        shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shader_stage_info.module = shader_module;
        shader_stage_info.pName = "main";

        VkComputePipelineCreateInfo pipeline_info = {};
        pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_info.stage = shader_stage_info;
        pipeline_info.layout = m_pipeline_layout;

        VK_CHECK(vkCreateComputePipelines(gpu.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &m_pipeline));
    }
}
void CullComputePass::bind_buffers(const GpuBuffer &object_buffer, const GpuBuffer &draw_buffer, const GpuBuffer &mesh_buffer) {
    m_descriptor_set.write_storage_buffer(0, 0, object_buffer.get(), 0, VK_WHOLE_SIZE);
    m_descriptor_set.write_storage_buffer(1, 0, draw_buffer.get(), 0, VK_WHOLE_SIZE);
    m_descriptor_set.write_storage_buffer(2, 0, mesh_buffer.get(), 0, VK_WHOLE_SIZE);
    m_descriptor_set.flush(*m_gpu);
}

void CullComputePass::run(WriteDependency &depends_on, u32 object_count) {
    // @todo: WAH! It feels really wrong for 2 reasons:
    //      1. waiting for the fence here is not good. I would want to do it someplace
    //         else.
    //      2. Dependencies should be handled outside of this function.
    ZoneScopedN("cull-lod-compute");
    {
        ZoneScopedN("wait-ready");
        VK_CHECK(vkWaitForFences(m_gpu->device, 1, &m_fence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(m_gpu->device, 1, &m_fence));
    }

    m_cmd.reset_begin();
    TracyVkCollect(m_cmd.tracy_ctx(), m_cmd.get());

    {
        TracyVkZone(m_cmd.tracy_ctx(), m_cmd.get(), "wait-host-write");
        depends_on.pipeline_barrier(m_cmd.get(),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT);
    }

    {
        TracyVkZone(m_cmd.tracy_ctx(), m_cmd.get(), "cull_lod_compute");

        VkDescriptorSet descriptor_sets[] = { m_descriptor_set.get() };

        vkCmdBindPipeline(m_cmd.get(), VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdBindDescriptorSets(m_cmd.get(), VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layout, 0,
            array_size(descriptor_sets), descriptor_sets, 0, nullptr);

        vkCmdDispatch(m_cmd.get(), (object_count + 15) / 16, 1, 1);
    }

    m_cmd.end();

    {
        // submit the command buffer
        VkCommandBuffer buffers[] = { m_cmd.get() };

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = array_size(buffers);
        submit_info.pCommandBuffers = buffers;
        vkQueueSubmit(m_gpu->compute_queue, 1, &submit_info, m_fence);
    }
}
