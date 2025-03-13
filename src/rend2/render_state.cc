#include "render_state.h"
#include "render.h"

#include "log.h"

#include <stb/stb_image.h>

#include <algorithm>

static logger_t logger = logger_t("renderstate");

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
    return mesh_handle;
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

    // make rendering pipeline
    // @todo: when changing the render target, we need to change this as well.
    // Not cool.
    auto pipeline_collection = m_renderer.create_pipelines_for(shader);
    return m_storage.store_shader(pipeline_collection);
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
