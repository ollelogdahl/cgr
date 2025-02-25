#pragma once

#include "gpu.h"
#include "oc.h"
#include "linalg.h"
#include "rend2/randomalloc.h"
#include "rend2/slotalloc.h"
#include "rend2/descriptor_set.h"
#include "rend2/buffer.h"
#include <set>
#include <vulkan/vulkan_core.h>

#include "render_handles.h"

struct RenderStateConfig {
    u32 max_objects;
    u32 max_vertices;
    u32 max_indices;
    u32 max_meshes;
    u32 max_materials;
    u32 max_textures;
};

class RenderState;

// @todo: ensure that these are glsl compatible.
#define MAX_LODS 4
struct alignas(16) MaterialData {
    v4f color;
};
struct alignas(16) GlobalData {
    m4f view;
    m4f proj;
    v3f view_pos;
    f32 _pad;
};
struct alignas(16) LODData {
    u32 index_start;
    u32 index_count;
    f32 distance;
};
struct alignas(16) MeshData {
    LODData lods[MAX_LODS];
    // @todo: add bounding shape
};
struct alignas(16) ObjectData {
    MaterialHandle material;
    MeshHandle mesh;
    m34f transform;
};

// @todo: Make parts of user data opaque to the render state.
// The size of vertices
class RenderState {
public:
    RenderState(gpu_t &gpu, const RenderStateConfig &config);

    VertexDataHandle alloc_vertices(slice<byte> vertices);
    IndexDataHandle alloc_indices(std::vector<u32> &&indices);
    MeshHandle alloc_mesh(const MeshData &);

    MaterialHandle alloc_material(const MaterialData &);
    TextureHandle alloc_texture(VkImageView view, VkSampler sampler);

    ObjectHandle alloc_object();

    const ObjectData &object_data(ObjectHandle handle);
    void update_object(ObjectHandle handle, const ObjectData &);

    void update_global(const GlobalData &);

    // flushes changes to the GPU.
    void flush(VkCommandBuffer cmd);

    VkBuffer object_buffer() const { return m_object_buffer.get(); }

    VkBuffer vertex_buffer() const { return m_vertex_buffer.get(); }
    VkBuffer index_buffer() const { return m_index_buffer.get(); }
    VkBuffer material_buffer() const { return m_material_buffer.get(); }
    VkBuffer global_buffer() const { return m_global_buffer.get(); }

    DescriptorSet global_descriptor_set() const { return m_global_ds; }
private:
    gpu_t *m_gpu;

    // objects are fun! They lie both on the CPU and the GPU.
    GpuBuffer m_object_buffer;
    ObjectData *m_object_data = nullptr;

    GpuBuffer m_vertex_buffer;
    GpuBuffer m_index_buffer;
    GpuBuffer m_material_buffer;
    GpuBuffer m_mesh_buffer;
    GpuBuffer m_global_buffer;

    RandomAllocator m_vertex_alloc;
    RandomAllocator m_index_alloc;
    SlotAllocator m_material_alloc;
    SlotAllocator m_mesh_alloc;
    SlotAllocator m_texture_alloc;
    SlotAllocator m_object_alloc;

    // @note: this could be split out into multiple sets.
    // Descriptor set 0 (global buffers and textures)
    //     binding 0: global buffer
    //     binding 1: mesh buffer
    //     binding 2: material buffer
    //     binding 3: texture descriptor array
    DescriptorSet m_global_ds;

    VkDescriptorPool m_descriptor_pool;

    std::set<ObjectHandle> dirty_objects;

    template <typename T>
    class WriteCache {
    public:
        struct CachedWrite {
            u32 offset;
            std::vector<T> data;
        };

        void insert(u32 offset, const T &data) {
            writes.push_back({offset, {data}});
        }
        void insert(CachedWrite &&write) {
            writes.push_back(std::move(write));
        }

        void clear() {
            writes.clear();
        }

        GpuBuffer::WriteList write_list() {
            gpu_writes = std::vector<GpuBuffer::Write>(writes.size());
            for (u32 i = 0; i < writes.size(); ++i) {
                auto &w = writes[i];
                byte *ptr = (byte *)w.data.data();
                auto len = w.data.size() * sizeof(T);
                gpu_writes.push_back(GpuBuffer::Write{
                    w.offset, std::span(ptr, len)
                });
            }
            return std::span(gpu_writes.begin(), gpu_writes.end());
        }
    private:
        std::vector<CachedWrite> writes;
        std::vector<GpuBuffer::Write> gpu_writes;
    };

    struct {
        WriteCache<byte> vertices;
        WriteCache<u32> indices;
        WriteCache<MaterialData> materials;
        WriteCache<MeshData> meshes;

        // just for this we could make immediate writes instead.
        GlobalData global_data;
        bool global_data_dirty = false;
    } m_writeback_buffers;
};
