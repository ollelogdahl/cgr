#pragma once

#include "rend2/buffer.h"
#include "rend2/command_buffer.h"
#include "rend2/descriptor_set.h"
#include "rend2/render_state.h"

#include "shader_compiler.h"

struct IndirectBatch {
    VkPipeline pipeline;
    u32 index;
    u32 count;
    u32 max_draws; //
};

struct RenderTarget {
    VkImageView color_view;
    VkImageView depth_view;
    VkExtent2D extent;
};

class ForwardIndirectPass {
public:
    // @todo: the descriptor set should really be private.
    // Ideally, the render state should just expose it's writer such that
    // one can apply the teture-writes to ones own descriptor set.
    //
    // well well.
    ForwardIndirectPass(gpu_t &gpu, ShaderCompiler &sc, u32 max_textures);

    void update_textures(std::span<TextureWrite> writes);
    void set_resources(VkBuffer global_buffer, VkBuffer object_buffer,
        VkBuffer draw_buffer,
        VkBuffer material_buffer);

    // preconditions:
    //  - index & vertex buffers bound.
    void record(CommandBuffer &cmd, const RenderTarget &target, std::span<IndirectBatch> batches);
private:
    gpu_t *m_gpu;

    DescriptorSet m_descriptor_set;

    VkBuffer m_draw_buffer;

    VkPipelineLayout m_pipeline_layout;
    VkPipeline m_pipeline;

    // pipeline query
    bool m_has_query_in_flight = false;
    bool m_started_query = false;
    VkQueryPool m_query_pool;
};
