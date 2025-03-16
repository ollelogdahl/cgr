#include "render.h"
#include "gpu.h"
#include "metrics.h"
#include "model.h"
#include "rend2/buffer.h"
#include "rend2/render_state.h"
#include "pipeline_layout_builder.h"
#include "descriptor_set_layout_builder.h"

#include <algorithm>
#include <vulkan/vulkan_core.h>

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
    void declare_buffer(TrackedResourceID id, VkBuffer buffer /* range */);
    void declare_image(TrackedResourceID id, VkImage image /* range */);

    void compute_write(TrackedResourceID resource);
    void compute_read(TrackedResourceID resource);

    void graphics_read(TrackedResourceID resource);
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

static VkPipeline make_render_pipeline(gpu_t &gpu, VkPipelineLayout layout, Shader shader);

Renderer::Renderer(gpu_t &gpu, RenderStorage &storage, ShaderCompiler &sc) : m_gpu(&gpu), m_storage(storage),
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

    m_forward_pipeline_layout = m_forward_pass.pipeline_layout();

    m_storage.render_descriptor_set().write_storage_buffer(4, 0, m_cluster_shading.cluster_buffer().get(), 0, VK_WHOLE_SIZE);
    m_storage.render_descriptor_set().write_storage_buffer(5, 0, m_cluster_shading.cluster_item_buffer().get(), 0, VK_WHOLE_SIZE);
    m_storage.render_descriptor_set().flush(gpu);
}

RenderStorage::ShaderInfo Renderer::create_pipelines_for(Shader &shader) {
    auto render_pipeline = make_render_pipeline(*m_gpu, m_forward_pipeline_layout, shader);
    return {
        .render_pipeline = render_pipeline,
    };
}

void Renderer::render(gpu_t::frame_t &frame, const View &view) {
    ZoneScoped;

    /*
    RenderTarget alloc_shadowtex(u32 size_factor);

    struct ShadowRenderTask {
        RenderTarget target;
        View view;
    };
    std::vector<ShadowRenderTask> shadow_render_tasks;

    m_storage.for_each_point_light([&](LightHandle _, LightData &light) {
        // determine detail level based on distance to camera.
        f32 dist_sq = view.position.square_distance(light.position.xyz());

        u32 size_factor = 5; // 512x512

        auto tex_handle = m_storage.reserve_contiguous_textures(6);

        const v3f forwards[] = {
            {1, 0, 0},
            {-1, 0, 0},
            {0, 1, 0},
            {0, -1, 0},
            {0, 0, 1},
            {0, 0, -1},
        };
        const v3f ups[] = {
            {0, 1, 0},
            {0, 1, 0},
            {0, 0, 1},
            {0, 0, -1},
            {0, 1, 0},
            {0, 1, 0},
        };

        for (uint i = 0; i < 6; ++i) {
            f32 near = 0.1;
            f32 far = 10.0;

            m4f view = m4f::look_at(
                light.position.xyz(), light.position.xyz() + forwards[i], ups[i]);
            m4f proj = m4f::perspective(anglef::from_deg(90), 1.0f, near, far);

            auto target = alloc_shadowtex(size_factor);

            shadow_render_tasks.push_back({
                .target = target,
                .view = {
                    .projection = proj,
                    .view = view,
                    .position = light.position.xyz(),
                    .znear = near,
                    .zfar = far,
                },
            });
        }

        light.shadowcast_texture_id = tex_handle;
        return true;
    });
    */

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

VkPipeline make_render_pipeline(gpu_t &gpu, VkPipelineLayout layout, Shader shader) {
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
            .format = VK_FORMAT_R8G8B8A8_UNORM,
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

    set_object_name(gpu, VK_OBJECT_TYPE_PIPELINE, pipeline, "render-forward");

    return pipeline;
}
