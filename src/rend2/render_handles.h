#pragma once

#include "oc.h"

class Renderer;
class RenderState;

class VertexDataHandle {
public:
    VertexDataHandle() : idx(-1), size(0) {}
    bool operator==(const VertexDataHandle &rhs) const { return idx == rhs.idx; }
    bool operator<(const VertexDataHandle &rhs) const { return idx < rhs.idx; }
private:
    VertexDataHandle(u32 idx, u32 size) : idx(idx), size(size) {}
    u32 idx;
    u32 size;

    friend class RenderState;
    friend class Renderer;
};
class IndexDataHandle {
public:
    IndexDataHandle() : idx(-1), size(0) {}
    bool operator==(const IndexDataHandle &rhs) const { return idx == rhs.idx; }
    bool operator<(const IndexDataHandle &rhs) const { return idx < rhs.idx; }
private:
    IndexDataHandle(u32 idx, u32 size) : idx(idx), size(size) {}
    u32 idx;
    u32 size;

    friend class RenderState;
    friend class Renderer;
};
class MaterialHandle {
public:
    MaterialHandle() : id(-1) {}
    bool operator==(const MaterialHandle &rhs) const { return id == rhs.id; }
    bool operator<(const MaterialHandle &rhs) const { return id < rhs.id; }
private:
    MaterialHandle(u32 id) : id(id) {}
    u32 id;

    friend class RenderState;
    friend class Renderer;
};
class TextureHandle {
public:
    TextureHandle() : id(-1) {}
    bool operator==(const TextureHandle &rhs) const { return id == rhs.id; }
    bool operator<(const TextureHandle &rhs) const { return id < rhs.id; }
private:
    TextureHandle(u32 id) : id(id) {}
    u32 id;

    friend class RenderState;
    friend class Renderer;
};

class MeshHandle {
public:
    MeshHandle() : id(-1) {}
    bool operator==(const MeshHandle &rhs) const { return id == rhs.id; }
    bool operator<(const MeshHandle &rhs) const { return id < rhs.id; }
private:
    MeshHandle(u32 id) : id(id) {}
    u32 id;

    friend class RenderState;
    friend class Renderer;
};

class ObjectHandle {
public:
    ObjectHandle() : id(-1) {}
    bool operator==(const ObjectHandle &rhs) const { return id == rhs.id; }
    bool operator<(const ObjectHandle &rhs) const { return id < rhs.id; }
private:
    ObjectHandle(u32 id) : id(id) {}
    u32 id;

    friend class RenderState;
    friend class Renderer;

    friend struct std::hash<ObjectHandle>;
};

template <>
struct std::hash<ObjectHandle> {
    std::size_t operator()(const ObjectHandle &handle) const {
        return std::hash<u32>()(handle.id);
    }
};

class ShaderHandle {
public:
    ShaderHandle() : id(-1) {}
    bool operator==(const ShaderHandle &rhs) const { return id == rhs.id; }
    bool operator<(const ShaderHandle &rhs) const { return id < rhs.id; }
private:
    ShaderHandle(u32 id) : id(id) {}
    u32 id;

    friend class RenderState;
    friend class Renderer;
};

class LightHandle {
public:
    LightHandle() : id(-1) {}
    bool operator==(const LightHandle &rhs) const { return id == rhs.id; }
    bool operator<(const LightHandle &rhs) const { return id < rhs.id; }
private:
    LightHandle(u32 id) : id(id) {}
    u32 id;

    friend class RenderState;
    friend class Renderer;
};

std::string format_as(VertexDataHandle);
std::string format_as(IndexDataHandle);
std::string format_as(MaterialHandle);
std::string format_as(TextureHandle);
std::string format_as(MeshHandle);
std::string format_as(ObjectHandle);
