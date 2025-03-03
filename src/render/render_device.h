#pragma once

#include "oc.h"
#include <span>

class RenderDevice;

// Base class for all GPU resource handles
class GpuResource {
protected:
    RenderDevice* device;
    void *handle;

    GpuResource(RenderDevice* dev, void *h) : device(dev), handle(h) {}
    friend class RenderDevice;
};

class Buffer : public GpuResource {
public:
    void clear(u64 offset, u64 size);
    void copy_to(Buffer& dst, u64 src_offset, u64 dst_offset, u64 size);

    std::span<byte> read(u64 offset = 0, u64 size = 0);
    void write(u64 offset, std::span<byte> data);

private:
    using GpuResource::GpuResource;
    friend class RenderDevice;
};

class Texture : public GpuResource {
private:
    using GpuResource::GpuResource;
    friend class RenderDevice;
};

class Shader : public GpuResource {
private:
    using GpuResource::GpuResource;
    friend class RenderDevice;
};

class UniformSet : public GpuResource {
public:
    void update(u32 binding, Buffer& buffer);
    void update_element(u32 binding, u32 element, Texture& texture);

private:
    using GpuResource::GpuResource;
    friend class RenderDevice;
};

class UniformBindingDecl {
public:
    static UniformBindingDecl storage_buffer(u32 binding) {
        return UniformBindingDecl{Type::StorageBuffer, binding, 1};
    }

    static UniformBindingDecl view_sampler_array(u32 max_count, u32 binding) {
        return UniformBindingDecl{Type::ViewSamplerArray, binding, max_count};
    }
private:
    enum class Type {
        StorageBuffer,
        ViewSamplerArray
    };
    UniformBindingDecl(Type t, u32 b, u32 s) : type(t), binding(b), array_size(s) {}

    Type type;
    u32 binding;
    u32 array_size;
};

class ComputePipeline : public GpuResource {
private:
    using GpuResource::GpuResource;
    friend class RenderDevice;
};

class DrawPipeline : public GpuResource {
private:
    using GpuResource::GpuResource;
    friend class RenderDevice;
};

class CommandList : public GpuResource {
protected:
    using GpuResource::GpuResource;
    friend class RenderDevice;
};

class DrawList : public CommandList {
public:
    void bind_pipeline(DrawPipeline& pipeline);
    void bind_uniform_set(UniformSet& set, u32 set_index);
    void push_label(std::string&& text);
    void pop_label();
    void bind_vertex_buffer(Buffer& buffer, u64 offset);
    void bind_index_buffer(Buffer& buffer, u64 offset);
    void set_push_constant(std::span<byte> data);
    void draw_indexed(u32 index_count, u32 instance_count, u32 first_index, u32 vertex_offset, u32 first_instance);
    void draw_indirect(Buffer& buffer, u64 offset, u32 draw_count, u32 stride);

private:
    using CommandList::CommandList;
    friend class RenderDevice;
};

class ComputeList : public CommandList {
public:
    void bind_pipeline(ComputePipeline& pipeline);
    void bind_uniform_set(UniformSet& set, u32 set_index);
    void set_push_constant(std::span<byte> data);
    void dispatch(u32 x, u32 y, u32 z);

private:
    using CommandList::CommandList;
    friend class RenderDevice;
};

class RenderDevice {
public:
    virtual ~RenderDevice() = default;

    // Factory methods
    virtual ComputePipeline create_compute_pipeline(Shader& shader) = 0;
    virtual DrawPipeline create_draw_pipeline(Shader& shader /* ... */) = 0;

    virtual Buffer create_uniform_buffer(u64 size) = 0;
    virtual Buffer create_storage_buffer(u64 size) = 0;
    virtual Buffer create_vertex_buffer(u64 size) = 0;
    virtual Buffer create_index_buffer(u64 size) = 0;

    virtual UniformSet create_uniform_set(std::span<const UniformBindingDecl> bindings) = 0;

    // Command list management
    virtual ComputeList begin_compute_list() = 0;
    virtual void end_compute_list(ComputeList& list) = 0;

    virtual DrawList begin_draw_list_for_screen() = 0;
    virtual void end_draw_list(DrawList& list) = 0;

    virtual void submit() = 0;

protected:
    // Protected implementation methods that derived classes can use
    virtual void buffer_clear(Buffer& buffer, u64 offset, u64 size) = 0;
    virtual void buffer_copy(Buffer& src, Buffer& dst, u64 src_offset, u64 dst_offset, u64 size) = 0;
    virtual std::span<byte> buffer_read(Buffer& buffer, u64 offset, u64 size) = 0;
    virtual void buffer_write(Buffer& buffer, u64 offset, std::span<byte> data) = 0;
    // ... other implementation methods for UniformSet, DrawList, etc.

    friend class Buffer;
    friend class UniformSet;
    friend class DrawList;
    friend class ComputeList;
};
