#include "forward_mesh_pass.h"
#include "metrics.h"
#include "rend2/pipeline_layout_builder.h"

#include <tracy/Tracy.hpp>

ForwardMeshPass::ForwardMeshPass(gpu_t &gpu, ShaderCompiler &sc, RenderStorage &state,
    GpuBuffer &draw_buffer)
: m_gpu(gpu), m_state(state), m_draw_buffer(draw_buffer), m_cull_pass(gpu, sc),
    m_pipeline_query(gpu) {

    m_cull_pass.bind_buffers(state.object_buffer(), draw_buffer, state.mesh_buffer());

    m_pipeline_layout = PipelineLayoutBuilder()
        .add_descriptor_set(state.render_descriptor_set().layout())
        .build(gpu);
}

void ForwardMeshPass::record(CommandBuffer &cmd, RenderTarget& target, const View& view,
        std::span<IndirectBatch2> batches) {
    ZoneScoped;
    TracyVkZone(cmd.tracy_ctx(), cmd.get(), "forward_mesh");

    m_cull_pass.update_view(view.position, view.view * view.projection);

    // bind the global descriptor set
    VkDescriptorSet descriptor_sets[] = { m_state.render_descriptor_set().get() };
    vkCmdBindDescriptorSets(cmd.get(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, 0,
        1, descriptor_sets, 0, nullptr);

    vkCmdBindIndexBuffer(cmd.get(), m_state.index_buffer().get(), 0, VK_INDEX_TYPE_UINT32);

    VkBuffer vertex_buffers[] = { m_state.vertex_buffer().get() };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(cmd.get(), 0, 1, vertex_buffers, offsets);

    VkViewport viewport = target.viewport();
    VkRect2D scissor = target.scissor();

    vkCmdSetViewport(cmd.get(), 0, 1, &viewport);
    vkCmdSetScissor(cmd.get(), 0, 1, &scissor);

    metrics::gauge_u64("rend2.forward.batches", batches.size());

    // @todo: here we need to decide how we should split the batches. For now,
    bool is_first = true;
    for (auto &batch : batches) {

        if (is_first) {
            target.clear_first = true;
        }
        else {
            target.clear_first = false;
            // create a wait before culling again; we need the draw to be finished.
            TracyVkZone(cmd.tracy_ctx(), cmd.get(), "wait-last-draw");
            WriteDependency wait_last_draw;
            wait_last_draw.add(
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                m_draw_buffer.get());
            wait_last_draw.pipeline_barrier(cmd.get(),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);
        }

        is_first = false;

        // run culling
        {
            TracyVkZone(cmd.tracy_ctx(), cmd.get(), "cull");
            m_cull_pass.record(cmd, m_state.highest_object_id() + 1);
        }

        WriteDependency cull_complete;
        cull_complete.add(
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT,
            m_draw_buffer.get());

        {
            TracyVkZone(cmd.tracy_ctx(), cmd.get(), "wait-cull-pass");
            cull_complete.pipeline_barrier(cmd.get(),
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        }

        {
            TracyVkZone(cmd.tracy_ctx(), cmd.get(), "draw");

            m_pipeline_query.begin(cmd);

            {
                auto results = m_pipeline_query.get_results();
                metrics::gauge_u64("rend2.forward.input_assembly_vertices", results[0]);
                metrics::gauge_u64("rend2.forward.input_assembly_primitives", results[1]);
                metrics::gauge_u64("rend2.forward.clipping_invocations", results[3]);
                metrics::gauge_u64("rend2.forward.clipping_primitives", results[4]);
                metrics::gauge_u64("rend2.forward.vertex_invocations", results[2]);
                metrics::gauge_u64("rend2.forward.fragment_invocations", results[5]);
            }

            VkRenderingAttachmentInfo color_attachments[] = {
                target.as_color_attachment()
            };
            VkRenderingAttachmentInfo depth_attachment = target.as_depth_attachment();

            VkRenderingInfo rendering_info{};
            rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering_info.renderArea = {
                .offset = {0, 0},
                .extent = target.extent
            };
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = array_size(color_attachments);
            rendering_info.pColorAttachments = color_attachments;
            rendering_info.pDepthAttachment = &depth_attachment;

            vkCmdBeginRendering(cmd.get(), &rendering_info);

            // draw
            vkCmdBindPipeline(cmd.get(), VK_PIPELINE_BIND_POINT_GRAPHICS, batch.pipeline);

            // remember that the draw-buffer is laid out like
            // [count][draw0     ][draw1      ]...[drawN      ]
            // u32    DrawCmd     DrawCmd
            vkCmdDrawIndexedIndirectCount(cmd.get(),
                m_draw_buffer.get(), batch.buffer_offset + sizeof(u32), m_draw_buffer.get(),
                batch.buffer_offset, batch.count,
                sizeof(DrawCommand));

            vkCmdEndRendering(cmd.get());

            m_pipeline_query.end(cmd);
        }
    }

}
