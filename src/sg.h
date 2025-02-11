#pragma once

#include <vector>

#include "oc.h"
#include "resource.h"
#include "linalg.h"

struct loader_t;

namespace sg {

class group_t;
class point_light_t;
class transform_t;
class geometry_t;
class camera_t;
class lod_t;

class node_visitor_t {
public:
    virtual void visit(group_t &group) = 0;
    virtual void visit(geometry_t &geometry) = 0;
    virtual void visit(point_light_t &point_light) = 0;
    virtual void visit(transform_t &transform) = 0;
    virtual void visit(camera_t &camera) = 0;
    virtual void visit(lod_t &lod) = 0;
};

class material_t {
public:
    v4f ambient;
    v4f diffuse;
    v4f specular;
    f32 roughness;
};

class state_t {
public:
    material_t material;
};

class node_t {
public:
    virtual ~node_t() = default;
    virtual void accept(node_visitor_t &visitor) = 0;

    aabb_t bounding_box() {
        return m_aabb;
    }

    state_t &state() {
        if (this->m_state) {
            return *this->m_state;
        } else {
            static state_t default_state = {
                .material = {
                    .ambient = {0.1f, 0.1f, 0.1f, 1.0f},
                    .diffuse = {0.5f, 0.5f, 0.5f, 1.0f},
                    .specular = {0.5f, 0.5f, 0.5f, 1.0f},
                    .roughness = 0.5f,
                },
            };
            return default_state;
        }
    }
    void set_state(state_t *state) {
        this->m_state = state;
    }

    void set_bounding_box(const aabb_t &aabb) {
        m_aabb = aabb;
    }

protected:
    state_t *m_state = nullptr;
    aabb_t m_aabb;
};

// specialized nodes
class group_t : public node_t {
public:
    virtual ~group_t() = default;

    void add(node_t *node) {
        m_children.push_back(node);
    }
    void accept_children(node_visitor_t &visitor) {
        for (auto &child : m_children) {
            child->accept(visitor);
        }
    }

    void accept(node_visitor_t &visitor) {
        visitor.visit(*this);
    }

    const std::vector<node_t *> &children() const {
        return m_children;
    }

protected:
    std::vector<node_t *> m_children;
};

class point_light_t : public node_t {
public:
    virtual ~point_light_t() = default;

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
    virtual ~transform_t() = default;

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
    virtual ~geometry_t() = default;

    geometry_t(ref_t<gpu_buffer_t> vertex_buffer, ref_t<gpu_buffer_t> index_buffer, u32 index_count)
        : vertex_buffer(vertex_buffer), index_buffer(index_buffer), index_count(index_count) {}

    void accept(node_visitor_t &visitor) {
        visitor.visit(*this);
    }

    ref_t<gpu_buffer_t> vertex_buffer;
    ref_t<gpu_buffer_t> index_buffer;
    u32 index_count;
};

class camera_t : public node_t {
public:
    virtual ~camera_t() = default;

    void accept(node_visitor_t &visitor) {
        visitor.visit(*this);
    }
};

class lod_t : public group_t {
public:
    virtual ~lod_t() = default;

    lod_t() : center{0, 0, 0}, ranges_min{} {}

    void accept(node_visitor_t &visitor) {
        visitor.visit(*this);
    }

    void set_center(const v3f &center) {
        this->center = center;
    }

    void set_ranges(const std::vector<f32> &ranges) {
        ranges_min = ranges;
    }

    void traverse(node_visitor_t &visitor);

private:
    v3f center;
    // @note: this needs the same length as the number of children.
    std::vector<f32> ranges_min;
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

    void clear();

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
    DECL_CREATOR(camera, camera_t, storage.cameras)
    DECL_CREATOR(lod, lod_t, storage.lods)

    DECL_CREATOR(state, state_t, storage.states)

#undef DECL_CREATOR

private:
    std::vector<node_t *> nodes;

    struct node_storage_t {
        pool_allocator_t<group_t> groups;
        pool_allocator_t<geometry_t> geometries;
        pool_allocator_t<point_light_t> point_lights;
        pool_allocator_t<transform_t> transforms;
        pool_allocator_t<camera_t> cameras;
        pool_allocator_t<lod_t> lods;

        pool_allocator_t<state_t> states;
    } storage;

    bool modified_on_disk = false;
    std::string disk_path;

    friend struct ::loader_t;
};

bool load(loader_t &loader, const char *path, scene_t &scene);

}
