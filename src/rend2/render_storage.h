#pragma once

#include "gpu.h"
#include "oc.h"
#include "linalg.h"
#include "rend2/command_buffer.h"
#include "rend2/randomalloc.h"
#include "rend2/slotalloc.h"
#include "rend2/descriptor_set.h"
#include "rend2/buffer.h"
#include <set>
#include <vulkan/vulkan_core.h>

#include "render_handles.h"

struct RenderStorageConfig {
    u32 max_objects;
    u32 max_vertices;
    u32 max_indices;
    u32 max_meshes;
    u32 max_materials;
    u32 max_textures;
    u32 max_lights;
};

class RenderStorage;

// @todo: ensure that these are glsl compatible.
#define MAX_LODS 4
struct alignas(16) MaterialData {
    v4f color;
    v4f emission;
    f32 roughness;
    f32 metallic;
    f32 tex_scale;

    TextureHandle tex_albedo0;
    TextureHandle tex_albedo1;
    TextureHandle tex_albedo2;
    TextureHandle tex_normal;
    TextureHandle tex_metallic;
    TextureHandle tex_roughness;
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
    v4f bounds_min;
    v4f bounds_max;
};

typedef u32 BatchId;

struct alignas(16) ObjectData {
    f32 transform[12]; // column-major affine transform 3x4 matrix
    MaterialHandle material;
    MeshHandle mesh;
    BatchId batch;
    u32 _pad;
};

struct alignas(16) LightData {
    v4f position; // w = 0 for directional light
    v4f color; // w intensity
    f32 falloff_linear;
    f32 falloff_quadratic;

    // nice we can put this here! :D
    TextureHandle shadowcast_texture_id;

    u32 _pad[1];
};

struct DrawCommand {
    VkDrawIndexedIndirectCommand indirect;
};

struct TextureWrite {
    u32 index;
    VkImageView view;
    VkSampler sampler;
};

struct TextureSlot {
    VkImage image;
    VkImageView view;
    VkSampler sampler;
};

// @todo: Make parts of user data opaque to the render state.
// The size of vertices
class RenderStorage {
public:
    RenderStorage(gpu_t &gpu, const RenderStorageConfig &config);

    // no move
    RenderStorage(RenderStorage &&) = delete;
    RenderStorage &operator=(RenderStorage &&) = delete;

    VertexDataHandle alloc_vertices(slice<byte> vertices);
    IndexDataHandle alloc_indices(std::vector<u32> &&indices);
    MeshHandle alloc_mesh(const MeshData &);

    MaterialHandle alloc_material(const MaterialData &);
    TextureHandle alloc_texture(VkImage image, VkImageView view, VkSampler sampler);

    TextureHandle reserve_contiguous_textures(u32 count);

    ObjectHandle alloc_object();

    const ObjectData &object_data(ObjectHandle handle);
    void update_object(ObjectHandle handle, const ObjectData &);

    // lights
    LightHandle alloc_light(const LightData &);
    const LightData &light_data(LightHandle handle);
    void update_light(LightHandle handle, const LightData &);

    void update_material(MaterialHandle handle, const MaterialData &);

    struct ShaderInfo {
        VkPipeline render_pipeline;
    };
    ShaderHandle store_shader(const ShaderInfo &info);

    void update_global(const GlobalData &);

    struct FlushDependencies {
        WriteDependency vertices;
        WriteDependency indices;
        WriteDependency materials;
        WriteDependency meshes;
        WriteDependency objects;
        WriteDependency global;
        WriteDependency lights;
        Dependency textures;
    };

    // flushes changes to the GPU.
    FlushDependencies flush(CommandBuffer &cmd);

    u32 highest_object_id() const { return m_highest_object_id; }

    // used by forward indirect pass for now; can maybe be moved?
    //
    // binding 0: global data (vert, frag)
    // binding 1: object data (vert)
    // binding 2: material data (frag)
    // binding 3: texture array (frag)
    DescriptorSet &render_descriptor_set() { return m_render_descriptor_set; }

    struct Batch {
        BatchId id;
        u32 start_index;
        u32 count;
    };
    std::span<const Batch> object_batches() const { return m_object_batches; }

    const GpuBuffer &object_buffer() const { return m_object_buffer; }
    const GpuBuffer &vertex_buffer() const { return m_vertex_buffer; }
    const GpuBuffer &index_buffer() const { return m_index_buffer; }
    const GpuBuffer &material_buffer() const { return m_material_buffer; }
    const GpuBuffer &mesh_buffer() const { return m_mesh_buffer; }
    const GpuBuffer &global_buffer() const { return m_global_buffer; }
    const GpuBuffer &light_buffer() const { return m_lights.buffer; }

    TextureSlot &texture(TextureHandle handle) { return m_textures[handle.id]; }

    std::span<const ShaderInfo> shaders() { return m_shaders; }


    void for_each_point_light(std::function<bool(LightHandle, LightData &)>);


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

    TextureSlot *m_textures = nullptr;

    RandomAllocator m_vertex_alloc;
    RandomAllocator m_index_alloc;
    SlotAllocator m_material_alloc;
    SlotAllocator m_mesh_alloc;
    SlotAllocator m_texture_alloc;
    SlotAllocator m_object_alloc;

    DescriptorSet m_render_descriptor_set;

    std::vector<Batch> m_object_batches;
    std::unordered_map<ObjectHandle, u32> m_object_to_idx_map;

    u32 m_highest_object_id = 0;

    std::vector<ShaderInfo> m_shaders;

    // @todo: same behaviour as object data; maybe break out into a CpuGpuSyncedBuffer?
    struct {
        GpuBuffer buffer;
        LightData *data = nullptr;
        SlotAllocator allocator;

        u32 highest_id = 0;
        std::set<LightHandle> dirty;
    } m_lights;

    struct {
        u32 vertices = 0;
        u32 indices = 0;
        u32 materials = 0;
        u32 meshes = 0;
        u32 objects = 0;
        u32 textures = 0;
        u32 lights = 0;
    } counts;

    std::set<ObjectHandle> dirty_objects;

    template <typename T>
    class WriteCache {
    public:
        struct CachedWrite {
            u32 offset;
            std::vector<T> data;
        };

        bool empty() const { return writes.empty(); }
        usize num_writes() const { return writes.size(); }
        usize total_bytes() const {
            usize total = 0;
            for (auto &w : writes) {
                total += w.data.size() * sizeof(T);
            }
            return total;
        }

        void insert(u32 offset, const T &data) {
            writes.push_back({offset, {data}});
        }
        void insert(CachedWrite &&write) {
            writes.push_back(std::move(write));
        }

        void clear() {
            writes.clear();
            gpu_writes.clear();
        }

        GpuBuffer::WriteList write_list() {
            gpu_writes = std::vector<GpuBuffer::Write>(writes.size());
            for (u32 i = 0; i < writes.size(); ++i) {
                auto &w = writes[i];
                byte *ptr = (byte *)w.data.data();
                auto len = w.data.size() * sizeof(T);
                gpu_writes[i] = GpuBuffer::Write{
                    w.offset, std::span(ptr, len)
                };
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
    } m_writes;
};
