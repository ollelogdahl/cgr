#include "render.h"
#include "gpu.h"
#include "metrics.h"
#include "rend2/buffer.h"
#include "rend2/forward_indirect_pass.h"
#include "rend2/render_state.h"
#include "pipeline_layout_builder.h"
#include "descriptor_set_layout_builder.h"

#include <algorithm>

#include "cull_lod_spv.h"
#include "rend2/shader_compiler.h"
#include "rend2/vku.h"
#include "tracy/Tracy.hpp"

static const RenderStateConfig config = {
    .max_objects = 1 * 1024 * 1024,
    .max_vertices = 1 * 1024 * 1024,
    .max_indices = 1 * 1024 * 1024,
    .max_meshes = 1024,
    .max_materials = 1024,
    .max_textures = 1024,
};
static const u32 max_draws = 1 * 1024 * 1024;

Renderer::Renderer(gpu_t &gpu) : m_gpu(&gpu), m_state(gpu, config),
        cull_pass(gpu), forward_pass(gpu, config.max_textures) {
    m_draw_buffer = GpuBuffer(gpu, max_draws * sizeof(DrawCommand) + 1 * sizeof(u32),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    cull_pass.bind_buffers(m_state.object_buffer(), m_draw_buffer, m_state.mesh_buffer());
    forward_pass.set_resources(
        m_state.global_buffer().get(),
        m_state.object_buffer().get(),
        m_draw_buffer.get(),
        m_state.material_buffer().get()
    );
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

    auto idx_handles = std::vector<IndexDataHandle>();
    idx_handles.reserve(mesh.lods.size());

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

    assert(mesh.lods.size() > 0);
    // @note: as MeshData currently works, this makes the vertex-data handle actually dangling.
    // In my current scenario this is fine (as i think we should try automatic resource reclaim),
    // but maybe not in the future.
    auto mesh_data = MeshData{};
    for (size_t i = 0; i < 4; i++) {
        auto src_idx = std::min(i, mesh.lods.size() - 1);
        mesh_data.lods[i] = {
            .index_start = idx_handles[src_idx].idx,
            .index_count = idx_handles[src_idx].size,
        };
    }

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

void Renderer::update_global(const GlobalData &data) {
    m_state.update_global(data);
}

void Renderer::render(gpu_t::frame_t &frame) {
    ZoneScoped;
    auto state_dependencies = m_state.flush(frame.cmd);

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
    WriteDependency cull_modified;
    {
        TracyVkZone(frame.cmd.tracy_ctx(), frame.cmd.get(), "wait-state-change");
        WriteDependency dependencies;
        dependencies.join(state_dependencies.objects);
        dependencies.join(state_dependencies.meshes);

        dependencies.pipeline_barrier(frame.cmd.get(),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    }

    {
        cull_pass.record(frame.cmd, m_state.highest_object_id() + 1);

        VkBufferMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barrier.buffer = m_draw_buffer.get();
        barrier.size = VK_WHOLE_SIZE;
        cull_modified.barriers.push_back(barrier);
    }

    // now we want to make another barrier. I guess in some way, the cull pass should
    // also return an array of changes done. We can call it ResourcePoke.
    {
        TracyVkZone(frame.cmd.tracy_ctx(), frame.cmd.get(), "wait-cull-pass");
        cull_modified.pipeline_barrier(frame.cmd.get(),
            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    }


    RenderTarget target = {
        .color_view = m_gpu->swapchain.image_views[frame.image_idx],
        .depth_view = m_gpu->depth_image.view,
        .extent = m_gpu->swapchain.extent,
    };

    VkBuffer vertex_buffers[] = {m_state.vertex_buffer().get()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(frame.cmd.get(), 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(frame.cmd.get(), m_state.index_buffer().get(), 0, VK_INDEX_TYPE_UINT32);

    std::vector<IndirectBatch> batches = {
        {
            .pipeline = 0,
            .index = 0,
            .count = 1,
            .max_draws = max_draws,
        }
    };
    metrics::gauge("rend2.batches", (u32)batches.size());

    forward_pass.record(frame.cmd, target, batches);
}

bool operator==(const LoadShaderProperties &lhs, const LoadShaderProperties &rhs) {
    return lhs.glsl_vert_path == rhs.glsl_vert_path && lhs.glsl_frag_path == rhs.glsl_frag_path;
}

std::size_t std::hash<LoadShaderProperties>::operator()(const LoadShaderProperties &props) const {
    return std::hash<const char *>()(props.glsl_vert_path) ^ std::hash<const char *>()(props.glsl_frag_path);
}

VkPipeline create_graphics_pipeline(gpu_t &gpu, VkPipelineLayout layout, const Shader &shader) {

    std::vector<VkFormat> color_attachment_formats = {
        VK_FORMAT_B8G8R8A8_UNORM
    };
    VkFormat depth_attachment_format = VK_FORMAT_D32_SFLOAT;

    std::vector<VkVertexInputBindingDescription> bindings = {
        {
            .binding = 0,
            .stride = 9 * sizeof(f32),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        }
    };
    std::vector<VkVertexInputAttributeDescription> attributes = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = 0,
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = 3 * sizeof(f32),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = 6 * sizeof(f32),
        },
        {
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = 8 * sizeof(f32),
        }
    };

    VkBool32 depth_test = VK_TRUE;
    VkBool32 depth_write = VK_TRUE;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;

    // boilerplate
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f; // Optional
    multisampling.pSampleMask = nullptr; // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE; // Optional

    VkPipelineRenderingCreateInfoKHR rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    rendering_info.colorAttachmentCount = color_attachment_formats.size();
    rendering_info.pColorAttachmentFormats = color_attachment_formats.data();
    rendering_info.depthAttachmentFormat = depth_attachment_format;
    rendering_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineViewportStateCreateInfo viewport_state{};
    {
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;
    }

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    {
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE; // useful to set as true for shadow mapping
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f; // Optional
        rasterizer.depthBiasClamp = 0.0f; // Optional
        rasterizer.depthBiasSlopeFactor = 0.0f; // Optional
    }

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY; // Optional
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &colorBlendAttachment;
    color_blending.blendConstants[0] = 0.0f; // Optional
    color_blending.blendConstants[1] = 0.0f; // Optional
    color_blending.blendConstants[2] = 0.0f; // Optional
    color_blending.blendConstants[3] = 0.0f; // Optional

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering_info;
    shader.apply_to(pipeline_info);

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    {
        vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input_info.vertexBindingDescriptionCount = bindings.size();
        vertex_input_info.pVertexBindingDescriptions = bindings.data();
        vertex_input_info.vertexAttributeDescriptionCount = attributes.size();
        vertex_input_info.pVertexAttributeDescriptions = attributes.data();

        pipeline_info.pVertexInputState = &vertex_input_info;
    }

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    {
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = 2;
        dynamic_state.pDynamicStates = dynamic_states;
    }

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state{};
    {
        depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil_state.depthTestEnable = depth_test;
        depth_stencil_state.depthWriteEnable = depth_write;
        depth_stencil_state.depthCompareOp = depth_compare_op;
        depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
        depth_stencil_state.minDepthBounds = 0.0f;
        depth_stencil_state.maxDepthBounds = 1.0f;
    }

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil_state;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = layout;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(gpu.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) != VK_SUCCESS) {
        // gpu_log.error("failed to create graphics pipeline");
    }

    return pipeline;
}
