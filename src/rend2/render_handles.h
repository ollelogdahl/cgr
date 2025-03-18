#pragma once

#include "oc.h"

class Renderer;
class RenderStorage;
class RenderState;

#define DECL_HASH_IMPL(type, key) \
    template <> struct std::hash<type> { \
        size_t operator()(const type &h) const { \
            return std::hash<decltype(h.key)>{}(h.key); \
        } \
    };

class VertexDataHandle {
public:
    VertexDataHandle() : idx(-1), size(0) {}
    bool operator==(const VertexDataHandle &rhs) const { return idx == rhs.idx; }
    bool operator<(const VertexDataHandle &rhs) const { return idx < rhs.idx; }
private:
    VertexDataHandle(u32 idx, u32 size) : idx(idx), size(size) {}
    u32 idx;
    u32 size;

    friend class RenderStorage;
    friend class Renderer;
    friend class RenderState;
    friend std::string format_as(VertexDataHandle);
    friend struct std::hash<VertexDataHandle>;
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

    friend class RenderStorage;
    friend class Renderer;
    friend class RenderState;
    friend std::string format_as(IndexDataHandle);
    friend struct std::hash<IndexDataHandle>;
};
class MaterialHandle {
public:
    MaterialHandle() : id(-1) {}
    bool operator==(const MaterialHandle &rhs) const { return id == rhs.id; }
    bool operator<(const MaterialHandle &rhs) const { return id < rhs.id; }
private:
    MaterialHandle(u32 id) : id(id) {}
    u32 id;

    friend class RenderStorage;
    friend class Renderer;
    friend class RenderState;
    friend std::string format_as(MaterialHandle);
    friend struct std::hash<MaterialHandle>;
};
class TextureHandle {
public:
    TextureHandle() : id(-1) {}
    bool operator==(const TextureHandle &rhs) const { return id == rhs.id; }
    bool operator<(const TextureHandle &rhs) const { return id < rhs.id; }
private:
    TextureHandle(u32 id) : id(id) {}
    u32 id;

    friend class RenderStorage;
    friend class Renderer;
    friend class RenderState;
    friend std::string format_as(TextureHandle);
    friend struct std::hash<TextureHandle>;
};

class MeshHandle {
public:
    MeshHandle() : id(-1) {}
    bool operator==(const MeshHandle &rhs) const { return id == rhs.id; }
    bool operator<(const MeshHandle &rhs) const { return id < rhs.id; }
private:
    MeshHandle(u32 id) : id(id) {}
    u32 id;

    friend class RenderStorage;
    friend class Renderer;
    friend class RenderState;
    friend std::string format_as(MeshHandle);
    friend struct std::hash<MeshHandle>;
};

class ObjectHandle {
public:
    ObjectHandle() : id(-1) {}
    bool operator==(const ObjectHandle &rhs) const { return id == rhs.id; }
    bool operator<(const ObjectHandle &rhs) const { return id < rhs.id; }
private:
    ObjectHandle(u32 id) : id(id) {}
    u32 id;

    friend class RenderStorage;
    friend class Renderer;
    friend class RenderState;

    friend struct std::hash<ObjectHandle>;
    friend std::string format_as(ObjectHandle);
};

class ShaderHandle {
public:
    ShaderHandle() : id(-1) {}
    bool operator==(const ShaderHandle &rhs) const { return id == rhs.id; }
    bool operator<(const ShaderHandle &rhs) const { return id < rhs.id; }
private:
    ShaderHandle(u32 id) : id(id) {}
    u32 id;

    friend class RenderStorage;
    friend class Renderer;
    friend class RenderState;
    friend std::string format_as(ShaderHandle);
    friend struct std::hash<ShaderHandle>;
};

class LightHandle {
public:
    LightHandle() : id(-1) {}
    bool operator==(const LightHandle &rhs) const { return id == rhs.id; }
    bool operator<(const LightHandle &rhs) const { return id < rhs.id; }
private:
    LightHandle(u32 id) : id(id) {}
    u32 id;

    friend class RenderStorage;
    friend class Renderer;
    friend class RenderState;
    friend std::string format_as(LightHandle);
    friend struct std::hash<LightHandle>;
};

std::string format_as(VertexDataHandle);
std::string format_as(IndexDataHandle);
std::string format_as(MaterialHandle);
std::string format_as(TextureHandle);
std::string format_as(MeshHandle);
std::string format_as(ObjectHandle);
std::string format_as(ShaderHandle);
std::string format_as(LightHandle);

DECL_HASH_IMPL(VertexDataHandle, idx)
DECL_HASH_IMPL(IndexDataHandle, idx)
DECL_HASH_IMPL(MaterialHandle, id)
DECL_HASH_IMPL(TextureHandle, id)
DECL_HASH_IMPL(MeshHandle, id)
DECL_HASH_IMPL(ObjectHandle, id)
DECL_HASH_IMPL(ShaderHandle, id)
DECL_HASH_IMPL(LightHandle, id)
