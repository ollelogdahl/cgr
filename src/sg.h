#pragma once

#include <vector>

#include "oc.h"
#include "resource.h"
#include "linalg.h"

namespace sg {

class node_visitor_t;

// im not fully convinced about this, we'll see.
class state_t {
    bool cull_face;
};

class node_t {
public:
    virtual void accept(node_visitor_t &visitor) = 0;
protected:
    state_t state;
};

class group_t;
class point_light_t;
class transform_t;
class geometry_t;

class node_visitor_t {
public:
    virtual void visit(group_t &group) = 0;
    virtual void visit(geometry_t &geometry) = 0;
    virtual void visit(point_light_t &point_light) = 0;
    virtual void visit(transform_t &transform) = 0;
};

// specialized nodes
class group_t : public node_t {
public:
    void add(node_t *node) {
        children.push_back(node);
    }
    void accept_children(node_visitor_t &visitor) {
        for (auto &child : children) {
            child->accept(visitor);
        }
    }

    void accept(node_visitor_t &visitor) {
        visitor.visit(*this);
    }
private:
    std::vector<node_t *> children;
};

class point_light_t : public node_t {
public:
    v3f position;
    v3f color;
    f32 linear;
    f32 quadratic;

    void accept(node_visitor_t &visitor) {
        visitor.visit(*this);
    }
};

class transform_t : public group_t {
public:
    m4f get_local_matrix() {
        auto rot = m4f::rotate(rotation_x, v3f{1, 0, 0})
            * m4f::rotate(rotation_y, v3f{0, 1, 0})
            * m4f::rotate(rotation_z, v3f{0, 0, 1});
        return m4f::scale(scale) * rot * m4f::translate(position);
    }

    void accept(node_visitor_t &visitor) {
        visitor.visit(*this);
    }

    v3f position = {0, 0, 0};
    anglef rotation_x = anglef::zero();
    anglef rotation_y = anglef::zero();
    anglef rotation_z = anglef::zero();
    v3f scale = {1, 1, 1};
};

class geometry_t : public node_t {
public:
    geometry_t(gpu_buffer_t vertex_buffer, gpu_buffer_t index_buffer, u32 index_count)
        : vertex_buffer(vertex_buffer), index_buffer(index_buffer), index_count(index_count) {}

    void accept(node_visitor_t &visitor) {
        visitor.visit(*this);
    }

    gpu_buffer_t vertex_buffer;
    gpu_buffer_t index_buffer;
    u32 index_count;
};

class scene_t {
public:
    scene_t() {}

    void add(node_t *node) {
        nodes.push_back(node);
    }
    void accept(node_visitor_t &visitor) {
        for (auto &node : nodes) {
            node->accept(visitor);
        }
    }

#define DECL_CREATOR(name, tname, storage) \
    template <typename ...Args> \
    tname *create_##name(Args... args) { \
        auto ptr = storage.alloc_make(args...); \
        return ptr; \
    }

    DECL_CREATOR(group, group_t, storage.groups)
    DECL_CREATOR(geometry, geometry_t, storage.geometries)
    DECL_CREATOR(point_light, point_light_t, storage.point_lights)
    DECL_CREATOR(transform, transform_t, storage.transforms)

#undef DECL_CREATOR

private:
    std::vector<node_t *> nodes;

    struct node_storage_t {
        pool_allocator_t<group_t> groups;
        pool_allocator_t<geometry_t> geometries;
        pool_allocator_t<point_light_t> point_lights;
        pool_allocator_t<transform_t> transforms;

    } storage;
};

}
