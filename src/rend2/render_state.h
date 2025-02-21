#pragma once

#include "gpu.h"
#include "oc.h"
#include "linalg.h"
#include "rend2/randomalloc.h"
#include "rend2/slotalloc.h"
#include "rend2/descriptor_set.h"
#include "rend2/buffer.h"
#include <vulkan/vulkan_core.h>

struct RenderStateConfig {
    u32 max_vertices;
    u32 max_indices;
    u32 max_materials;
    u32 max_textures;
    u32 max_transforms;
};

class RenderState;

class VertexDataHandle {
private:
    VertexDataHandle(u32 id, u32 size) : id(id), size(size) {}
    u32 id;
    u32 size;
    friend class RenderState;
};
class IndexDataHandle {
private:
    IndexDataHandle(u32 id, u32 size) : id(id), size(size) {}
    u32 id;
    u32 size;
    friend class RenderState;
};
class MaterialHandle {
private:
    MaterialHandle(u32 id) : id(id) {}
    u32 id;
    friend class RenderState;
};
class TextureHandle {

private:
    TextureHandle(u32 id) : id(id) {}
    u32 id;
    friend class RenderState;
};
class TransformHandle {
private:
    TransformHandle(u32 id) : id(id) {}
    u32 id;
    friend class RenderState;
};

std::string format_as(VertexDataHandle);
std::string format_as(IndexDataHandle);
std::string format_as(MaterialHandle);
std::string format_as(TextureHandle);
std::string format_as(TransformHandle);

struct TextureData {
    VkImageView view;
    VkSampler sampler;
};

// @todo: ensure that these are glsl compatible.
struct MaterialData {
    v4f color;
};
struct GlobalData {
    m4f view;
    m4f proj;
    v3f view_pos;
    f32 _pad;
};

struct TransformData {

};

// @todo: Make parts of user data opaque to the render state.
// The size of vertices
class RenderState {
public:
    RenderState(gpu_t &gpu, const RenderStateConfig &config);

    // returns the start index of the span.
    VertexDataHandle alloc_vertices(slice<byte> vertices);
    IndexDataHandle alloc_indices(slice<u32> indices);

    MaterialHandle alloc_material(const MaterialData &);
    TextureHandle alloc_texture(VkImageView view, VkSampler sampler);
    TransformHandle alloc_transform();

    void update_transform(TransformHandle handle, const TransformData &);
    void update_global(const GlobalData &);

    // flushes changes to the GPU.
    void flush(VkCommandBuffer cmd);

    void bind(VkCommandBuffer cmd, VkPipelineLayout layout) const;
    VkDescriptorSetLayout global_descriptor_set_layout() const { return m_global_ds.layout(); }
private:
    gpu_t *m_gpu;
    GpuBuffer m_vertex_buffer;
    GpuBuffer m_index_buffer;
    GpuBuffer m_material_buffer;
    GpuBuffer m_transform_buffer;
    GpuBuffer m_global_buffer;

    RandomAllocator m_vertex_alloc;
    RandomAllocator m_index_alloc;
    SlotAllocator m_material_alloc;
    SlotAllocator m_transform_alloc;
    SlotAllocator m_texture_alloc;

    // @note: this could be split out into multiple sets.
    // Descriptor set 0 (global buffers and textures)
    //     binding 0: global buffer
    //     binding 1: material buffer
    //     binding 2: transform buffer
    //     binding 3: texture descriptor array
    DescriptorSet m_global_ds;

    VkDescriptorPool m_descriptor_pool;

    struct {
        GpuBuffer::WriteList vertices;
        GpuBuffer::WriteList indices;
        GpuBuffer::WriteList materials;
        GpuBuffer::WriteList transforms;

        // just for this we could make immediate writes instead.
        std::vector<byte> global_data;
    } writeback_buffers;
};
