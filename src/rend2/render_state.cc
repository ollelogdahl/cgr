#include "render_state.h"
#include "rend2/command_buffer.h"
#include "rend2/vku.h"
#include "render.h"

#include "log.h"

#include <stb/stb_image.h>

#include <algorithm>

static logger_t logger = logger_t("renderstate");

static VkPipeline make_render_pipeline(gpu_t &gpu, VkPipelineLayout layout, Shader shader);

MeshHandle RenderState::add_mesh(const Mesh &mesh) {
    // 1. interleave vertex properties.
    // 2. calculate the index offset (where the vertices were allocated), and offset all indices.

    // @todo: Using manual vertex fetching, we could actually support different
    // types of vertex data. This could be really nice, as we could save memory.
    // According to some sources, manual fetching is not too slow.

    bool uv_present = mesh.uvs.size() > 0;
    bool color_present = mesh.colors.size() > 0;
    auto interleaved = std::vector<byte>(mesh.vertices.size() * 9 * sizeof(f32));
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        auto &v = mesh.vertices[i];
        auto &n = mesh.normals[i];

        memcpy(&interleaved[i * 9 * sizeof(f32)], &v, sizeof(v));
        memcpy(&interleaved[i * 9 * sizeof(f32) + 3 * sizeof(f32)], &n, sizeof(n));

        if (uv_present) {
            auto &t = mesh.uvs[i];
            memcpy(&interleaved[i * 9 * sizeof(f32) + 6 * sizeof(f32)], &t, sizeof(t));
        }

        if (color_present) {
            auto &c = mesh.colors[i];
            memcpy(&interleaved[i * 9 * sizeof(f32) + 8 * sizeof(f32)], &c, sizeof(c));
        }
    }

    auto vertex_handle = m_storage.alloc_vertices(slice<byte>(interleaved));

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
            m_storage.alloc_indices(std::move(indices))
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
            .distance = mesh.lods[src_idx].min_distance,
        };
    }

    mesh_data.bounds_min = mesh.bounds.min.to_homogeneous();
    mesh_data.bounds_max = mesh.bounds.max.to_homogeneous();

    auto mesh_handle = m_storage.alloc_mesh(mesh_data);

    m_blas_tasks.push_back(BlasBuildTask{
        .handle = mesh_handle,
        .data = mesh_data,
        .num_vertices = (u32)mesh.vertices.size(),
    });

    return mesh_handle;
}

void RenderState::finalize_before_render(CommandBuffer &cmd) {
    for (auto &m : m_blas_tasks) {
        for (auto i = 0; i < 4; ++i) {
            auto &lod = m.data.lods[i];
            m_storage.acceleration_builder().build_blas(
                cmd,
                m_storage.vertex_buffer(),
                m_storage.index_buffer(),
                0,
                lod.index_start,
                m.num_vertices,
                lod.index_count,
                9 * sizeof(f32));
        }
    }

    m_blas_tasks.clear();
    m_storage.acceleration_builder().await_build(cmd);
}

MaterialHandle RenderState::add_material(const MaterialData &data) {
    return m_storage.alloc_material(data);
}

ShaderHandle RenderState::load_shader(const LoadShaderProperties &props) {
    auto it = m_shader_cache.find(props);
    if (it != m_shader_cache.end()) {
        return it->second;
    }

    auto shader = Shader({
        m_shader_compiler.compile(props.glsl_vert_path),
        m_shader_compiler.compile(props.glsl_frag_path),
    });

    auto render_pipeline = make_render_pipeline(m_gpu, m_forward_pipeline_layout, shader);

    return m_storage.store_shader({
        .render_pipeline = render_pipeline,
    });
}

TextureHandle RenderState::load_texture(const LoadTextureProperties &props) {
    auto it = m_texture_cache.find(props);
    if (it != m_texture_cache.end()) {
        return it->second;
    }

    logger.info("Loading texture: {}", props.path);

    VkImage image;
    VkSampler sampler;
    VkImageView view;
    {
        VkFormat format;
        int target_channels;
        switch (props.type) {
        case TextureType::R:
            target_channels = 1;
            format = VK_FORMAT_R8_UNORM;
            break;
        case TextureType::RGB:
            target_channels = 3;
            format = VK_FORMAT_R8G8B8_UNORM;
            break;
        case TextureType::SRGB:
            target_channels = 3;
            format = VK_FORMAT_R8G8B8_SRGB;
            break;
        case TextureType::RGBA:
            target_channels = 4;
            format = VK_FORMAT_R8G8B8A8_UNORM;
            break;
        case TextureType::SRGBA:
            target_channels = 4;
            format = VK_FORMAT_R8G8B8A8_SRGB;
            break;
        }

        // @todo: break this stuff out!
        int width, height, channels;
        auto ptr = stbi_load(props.path, &width, &height, &channels, target_channels);

        auto img_data = slice<u8>(ptr, width * height * target_channels);

        gpu_image_t img;
        m_gpu.create_image(
            img_data,
            width,
            height,
            format,
            VK_IMAGE_USAGE_SAMPLED_BIT,
            false,
            img);

        image = img.image;

        auto sampler_info = VkSamplerCreateInfo{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.mipLodBias = 0.0f;
        sampler_info.maxAnisotropy = 1.0f;
        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = 1.0f;
        sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        sampler_info.unnormalizedCoordinates = false;

        vkCreateSampler(
            m_gpu.device,
            &sampler_info,
            nullptr,
            &sampler);

        auto view_info = VkImageViewCreateInfo{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format;
        view_info.components.r = VK_COMPONENT_SWIZZLE_R;
        view_info.components.g = VK_COMPONENT_SWIZZLE_G;
        view_info.components.b = VK_COMPONENT_SWIZZLE_B;
        view_info.components.a = VK_COMPONENT_SWIZZLE_A;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        vkCreateImageView(
            m_gpu.device,
            &view_info,
            nullptr,
            &view);
    }

    auto texture = m_storage.alloc_texture(image, view, sampler);
    m_texture_cache[props] = texture;
    return texture;
}

void RenderState::update_material(MaterialHandle handle, const MaterialData &data) {
    m_storage.update_material(handle, data);
}

ObjectHandle RenderState::add_object() {
    // each object has a unique transform for now
    auto object = m_storage.alloc_object();

    return object;
}

void RenderState::assign_geometry(ObjectHandle handle, MeshHandle mesh) {
    auto old = m_storage.object_data(handle);
    old.mesh = mesh;
    m_storage.update_object(handle, old);
}

void RenderState::assign_material(ObjectHandle handle, MaterialHandle material) {
    auto old = m_storage.object_data(handle);
    old.material = material;
    m_storage.update_object(handle, old);
}

void RenderState::assign_shader(ObjectHandle handle, ShaderHandle shader) {
    auto old = m_storage.object_data(handle);
    old.batch = shader.id;
    m_storage.update_object(handle, old);
}

void assign_packed_affine_transformation(f32 *dest, const m4f &m) {
    // column-major
    dest[0] = m.m[0];
    dest[1] = m.m[1];
    dest[2] = m.m[2];
    dest[3] = m.m[4];
    dest[4] = m.m[5];
    dest[5] = m.m[6];
    dest[6] = m.m[8];
    dest[7] = m.m[9];
    dest[8] = m.m[10];
    dest[9] = m.m[12];
    dest[10] = m.m[13];
    dest[11] = m.m[14];
}

void RenderState::update_transform(ObjectHandle handle, const m4f &t) {
    auto o = m_storage.object_data(handle);
    assign_packed_affine_transformation(o.transform, t);
    m_storage.update_object(handle, o);
}

LightHandle RenderState::add_light(const LightData &data) {
    return m_storage.alloc_light(data);
}

void RenderState::update_light(LightHandle handle, const LightData &data) {
    m_storage.update_light(handle, data);
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
