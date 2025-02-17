#pragma once

#include <vector>
#include <functional>

#include "oc.h"
#include "linalg.h"

#include "material.h"

#include <tracy/Tracy.hpp>

struct loader_t;

namespace sg {

class node_t;
class group_t;
class point_light_t;
class transform_t;
class geometry_t;
class camera_t;
class lod_t;

class node_visitor_t;

class state_t {
public:
    ref_t<material_t> material;
};

typedef std::function<void(node_t &)> update_callback_t;

class node_t {
public:
    virtual ~node_t() = default;
    virtual void accept(node_visitor_t &visitor) = 0;

    virtual void reset_to_initial_state() {}

    state_t &state() {
        return *m_state;
    }
    void set_state(state_t *state) {
        this->m_state = state;
    }

    aabb_t bounding_box() {
        return m_aabb;
    }
    void set_bounding_box(const aabb_t &aabb) {
        m_aabb = aabb;
    }

    void update() {
        if (m_update_callback) {
            m_update_callback(*this);
        }
    }
    void set_update_callback(update_callback_t callback) {
        m_update_callback = callback;
    }

protected:
    update_callback_t m_update_callback = nullptr;
    state_t *m_state = nullptr;
    aabb_t m_aabb;
};

class node_visitor_t {
public:

    virtual void visit(geometry_t &geometry) {}
    virtual void visit(point_light_t &point_light) {}
    virtual void visit(camera_t &camera) {}

    virtual void visit(group_t &group);
    virtual void visit(transform_t &transform);
    virtual void visit(lod_t &lod);
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

    void accept(node_visitor_t &visitor) override {
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
        if (m_dirty) {
            auto rot = m4f::rotate(rotation_x, v3f{1, 0, 0})
                * m4f::rotate(rotation_y, v3f{0, 1, 0})
                * m4f::rotate(rotation_z, v3f{0, 0, 1});
            m_local_matrix = m4f::scale(scale) * rot * m4f::translate(translation);
            m_dirty = false;
        }
        return m_local_matrix;
    }

    void reset_to_initial_state() override {
        translation = initial_transform.translation;
        rotation_x = initial_transform.rotation_x;
        rotation_y = initial_transform.rotation_y;
        rotation_z = initial_transform.rotation_z;
        scale = initial_transform.scale;
        m_dirty = true;
    }

    void accept(node_visitor_t &visitor) override {
        visitor.visit(*this);
    }

    void set_initial_transform(const v3f &translation, const v3f &euler_deg, const v3f &scale) {
        initial_transform.translation = translation;
        initial_transform.rotation_x = anglef::from_deg(euler_deg.x);
        initial_transform.rotation_y = anglef::from_deg(euler_deg.y);
        initial_transform.rotation_z = anglef::from_deg(euler_deg.z);
        initial_transform.scale = scale;

        reset_to_initial_state();
    }

    void set_translation(const v3f &translation) {
        this->translation = translation;
        m_dirty = true;
    }

    void set_euler_rotation(const v3f &euler_deg) {
        rotation_x = anglef::from_deg(euler_deg.x);
        rotation_y = anglef::from_deg(euler_deg.y);
        rotation_z = anglef::from_deg(euler_deg.z);
        m_dirty = true;
    }

    void set_scale(const v3f &scale) {
        this->scale = scale;
        m_dirty = true;
    }

private:
    v3f translation = {0, 0, 0};
    anglef rotation_x = anglef::zero();
    v3f scale = {1, 1, 1};
    anglef rotation_y = anglef::zero();
    m4f m_local_matrix;
    anglef rotation_z = anglef::zero();
    bool m_dirty = true;

    struct {
        v3f translation = {0, 0, 0};
        anglef rotation_x = anglef::zero();
        v3f scale = {1, 1, 1};
        anglef rotation_y = anglef::zero();
        anglef rotation_z = anglef::zero();
    } initial_transform;

};

class geometry_t : public node_t {
public:
    virtual ~geometry_t() = default;

    geometry_t(ref_t<gpu_buffer_t> vertex_buffer, ref_t<gpu_buffer_t> index_buffer, u32 index_count)
        : vertex_buffer(vertex_buffer), index_buffer(index_buffer), index_count(index_count) {}

    void accept(node_visitor_t &visitor) override {
        visitor.visit(*this);
    }

    ref_t<gpu_buffer_t> vertex_buffer;
    ref_t<gpu_buffer_t> index_buffer;
    u32 index_count;
};

// @note: camera doesn't care about it's surrounding transforms.
class camera_t : public node_t {
public:
    virtual ~camera_t() = default;

    v3f position() const {
        return m_position;
    }
    v3f forward() const {
        return m_forward;
    }
    v3f up() const {
        return m_up;
    }
    v3f target() const {
        return m_position + m_forward;
    }
    bool controlled() const {
        return m_controlled;
    }

    m4f view_matrix() {
        if (m_dirty) {
            m_view_matrix = m4f::look_at(m_position, m_position + m_forward, m_up);
            m_dirty = false;
        }

        return m_view_matrix;
    }

    void set_position(const v3f &position) {
        m_position = position;
        m_dirty = true;
    }
    void set_forward(const v3f &forward) {
        m_forward = forward;
        m_dirty = true;
    }
    void set_up(const v3f &up) {
        m_up = up;
        m_dirty = true;
    }
    void set_target(const v3f &target) {
        m_forward = v3f::normalize(target - m_position);
        m_dirty = true;
    }

    void set_controlled(bool controlled) {
        m_controlled = controlled;
    }

    void set_initial_state(const v3f &position, const v3f &target, const v3f &up) {
        m_initial.position = position;
        m_initial.forward = v3f::normalize(target - position);
        m_initial.up = up;
        reset_to_initial_state();
    }

    void accept(node_visitor_t &visitor) override {
        visitor.visit(*this);
    }

    void reset_to_initial_state() override {
        m_position = m_initial.position;
        m_forward = m_initial.forward;
        m_up = m_initial.up;
        m_dirty = true;
    }
private:
    m4f m_view_matrix;
    v3f m_position;
    v3f m_forward;
    v3f m_up;
    bool m_dirty = true;
    bool m_controlled = false;

    struct {
        v3f position;
        v3f forward;
        v3f up;
    } m_initial;
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
    scene_t() {
        m_default_material = create_material();
        m_default_material->ambient = {0.1f, 0.1f, 0.1f, 1.0f};
        m_default_material->diffuse = {0.5f, 0.5f, 0.5f, 1.0f};
        m_default_material->specular = {0.5f, 0.5f, 0.5f, 1.0f};
        m_default_material->roughness = 0.5f;

        m_default_state.material = m_default_material;
    }

    void add(node_t *node) {
        nodes.push_back(node);
    }
    void accept(node_visitor_t &visitor) {
        ZoneScopedN("scene-graph-accept");
        for (auto &node : nodes) {
            node->accept(visitor);
        }
    }

    void clear();

    // resets the scene to its initial state.
    void reset_to_initial_state();

#define DECL_CREATOR(name, tname, storage) \
    template <typename ...Args> \
    tname *create_##name(Args... args) { \
        auto ptr = storage.alloc_make(args...); \
        ptr->set_state(&m_default_state); \
        return ptr; \
    }

    DECL_CREATOR(group, group_t, storage.groups)
    DECL_CREATOR(geometry, geometry_t, storage.geometries)
    DECL_CREATOR(point_light, point_light_t, storage.point_lights)
    DECL_CREATOR(transform, transform_t, storage.transforms)
    DECL_CREATOR(camera, camera_t, storage.cameras)
    DECL_CREATOR(lod, lod_t, storage.lods)
#undef DECL_CREATOR

    state_t *create_state() {
        auto ptr = storage.states.alloc_make();
        ptr->material = m_default_material;
        return ptr;
    }

    ref_t<material_t> create_material() {
        auto ptr = make_ref<material_t>();
        storage.materials.push_back(ptr);
        return ptr;
    }

    ref_t<material_t> default_material() {
        return m_default_material;
    }

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
        std::vector<ref_t<material_t>> materials;
    } storage;

    state_t m_default_state;
    ref_t<material_t> m_default_material;

    bool modified_on_disk = false;
    std::string disk_path;

    friend struct ::loader_t;
};

bool load(loader_t &loader, const char *path, scene_t &scene);

}
