#pragma once

#include <vector>
#include <functional>

#include "model.h"
#include "oc.h"
#include "linalg.h"

#include "rend2/render_handles.h"
#include "rend2/render_state.h"
#include "rend2/render_storage.h"

#include <tracy/Tracy.hpp>

struct loader_t;

namespace sg {

class node_t;
class group_t;
class point_light_t;
class directional_light_t;
class transform_t;
class geometry_t;
class camera_t;

class node_visitor_t;

typedef std::function<void(node_t &)> update_callback_t;

class Material {
public:
    Material(RenderState &state) : m_state(state) {
        m_handle = m_state.add_material(m_data);
    }

    const v4f &color() {
        return m_data.color;
    }
    const v4f &emission() {
        return m_data.emission;
    }
    const f32 &roughness() {
        return m_data.roughness;
    }
    const f32 &metallic() {
        return m_data.metallic;
    }

    void set_color(const v4f &color) {
        m_data.color = color;
        m_state.update_material(m_handle, m_data);
    }
    void set_emission(const v4f &emission) {
        m_data.emission = emission;
        m_state.update_material(m_handle, m_data);
    }
    void set_roughness(f32 roughness) {
        m_data.roughness = roughness;
        m_state.update_material(m_handle, m_data);
    }
    void set_metallic(f32 metallic) {
        m_data.metallic = metallic;
        m_state.update_material(m_handle, m_data);
    }

    void set_texture_scale(f32 scale) {
        m_data.tex_scale = scale;
        m_state.update_material(m_handle, m_data);
    }

    void set_shader(ShaderHandle shader) {
        m_shader = shader;
    }

    void set_albedo0(TextureHandle albedo0) {
        m_data.tex_albedo0 = albedo0;
        m_state.update_material(m_handle, m_data);
    }
    void set_albedo1(TextureHandle albedo1) {
        m_data.tex_albedo1 = albedo1;
        m_state.update_material(m_handle, m_data);
    }
    void set_albedo2(TextureHandle albedo2) {
        m_data.tex_albedo2 = albedo2;
        m_state.update_material(m_handle, m_data);
    }
    void set_normal(TextureHandle normal) {
        m_data.tex_normal = normal;
        m_state.update_material(m_handle, m_data);
    }
    void set_metallic(TextureHandle metallic) {
        m_data.tex_metallic = metallic;
        m_state.update_material(m_handle, m_data);
    }
    void set_roughness(TextureHandle roughness) {
        m_data.tex_roughness = roughness;
        m_state.update_material(m_handle, m_data);
    }

    MaterialHandle handle() {
        return m_handle;
    }
    ShaderHandle shader() {
        return m_shader;
    }
private:
    MaterialData m_data;
    MaterialHandle m_handle;
    ShaderHandle m_shader;
    RenderState &m_state;
};

class node_t {
public:
    virtual ~node_t() = default;
    virtual void accept(node_visitor_t &visitor) = 0;

    virtual void reset_to_initial_state() {}

    aabb_t bounding_box() {
        return m_aabb;
    }
    void set_bounding_box(const aabb_t &aabb) {
        m_aabb = aabb;
    }

    void update() {
        if (m_update_callback) {
            m_update_callback(*this);
            m_dirty = true;
        }
    }
    void set_update_callback(update_callback_t callback) {
        m_update_callback = callback;
    }

    bool is_dirty() const {
        return m_dirty;
    }
    void mark_dirty() {
        m_dirty = true;
        if (m_parent) {
            m_parent->mark_dirty();
        }
    }
    void unmark_dirty() {
        m_dirty = false;
    }

protected:
    update_callback_t m_update_callback = nullptr;
    aabb_t m_aabb;
    node_t *m_parent = nullptr;
    bool m_dirty = true;
    friend class group_t;
};

class node_visitor_t {
public:
    virtual void visit(geometry_t &geometry) {}
    virtual void visit(point_light_t &point_light) {}
    virtual void visit(directional_light_t &directional_light) {}
    virtual void visit(camera_t &camera) {}

    virtual void visit(group_t &group);
    virtual void visit(transform_t &transform);
};

// specialized nodes
class group_t : public node_t {
public:
    virtual ~group_t() = default;

    void add(node_t *node) {
        node->m_parent = this;
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

    void clear_children() {
        m_children.clear();
    }

protected:
    std::vector<node_t *> m_children;
};

class point_light_t : public node_t {
public:
    point_light_t(RenderState &state) : m_state(state) {
        m_data.position = {0, 0, 0, 1};
        m_data.color = {1, 1, 1, 1};
        m_data.falloff_linear = 0.14f;
        m_data.falloff_quadratic = 0.07f;
        m_handle = m_state.add_light(m_data);
    }
    virtual ~point_light_t() = default;

    void set_color(const v3f &color) {
        if (color != m_data.color.xyz()) {
            m_data.color.x = color.x;
            m_data.color.y = color.y;
            m_data.color.z = color.z;
            m_state.update_light(m_handle, m_data);
        }
    }
    void set_intensity(f32 intensity) {
        if (intensity != m_data.color.w) {
            m_data.color.w = intensity;
            m_state.update_light(m_handle, m_data);
        }
    }

    void set_position(const v3f &position) {
        if (position != m_data.position.xyz()) {
            m_data.position = position.to_homogeneous();
            m_state.update_light(m_handle, m_data);
        }
    }

    void set_falloff_linear(f32 falloff) {
        if (falloff != m_data.falloff_linear) {
            m_data.falloff_linear = falloff;
            m_state.update_light(m_handle, m_data);
        }
    }

    void set_falloff_quadratic(f32 falloff) {
        if (falloff != m_data.falloff_quadratic) {
            m_data.falloff_quadratic = falloff;
            m_state.update_light(m_handle, m_data);
        }
    }

    void accept(node_visitor_t &visitor) {
        visitor.visit(*this);
    }
private:
    RenderState &m_state;
    LightData m_data;
    LightHandle m_handle;
};

class directional_light_t : public node_t {
public:
    virtual ~directional_light_t() = default;
    void accept(node_visitor_t &visitor) override {
        visitor.visit(*this);
    }

    v3f direction() const {
        return m_direction;
    }

    v3f color() const {
        return m_color;
    }

    void set_direction(const v3f &direction) {
        m_direction = direction;
    }

    void set_color(const v3f &color) {
        m_color = color;
    }

private:
    v3f m_direction;
    v3f m_color;
};

class transform_t : public group_t {
public:
    virtual ~transform_t() = default;


    m4f get_local_matrix() {
        if (is_dirty()) {
            auto rot = m4f::rotate(m_rotation_x, v3f{1, 0, 0})
                * m4f::rotate(m_rotation_y, v3f{0, 1, 0})
                * m4f::rotate(m_rotation_z, v3f{0, 0, 1});
            m_local_matrix = m4f::scale(m_scale) * rot * m4f::translate(m_translation);
            m_dirty = false;
        }
        return m_local_matrix;
    }

    void reset_to_initial_state() override {
        m_translation = initial_transform.translation;
        m_rotation_x = initial_transform.rotation_x;
        m_rotation_y = initial_transform.rotation_y;
        m_rotation_z = initial_transform.rotation_z;
        m_scale = initial_transform.scale;
        mark_dirty();
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
        this->m_translation = translation;
        mark_dirty();
    }

    void set_euler_rotation(const v3f &euler_deg) {
        m_rotation_x = anglef::from_deg(euler_deg.x);
        m_rotation_y = anglef::from_deg(euler_deg.y);
        m_rotation_z = anglef::from_deg(euler_deg.z);
        mark_dirty();
    }

    v3f euler_rotation() {
        return {m_rotation_x.as_deg(), m_rotation_y.as_deg(), m_rotation_z.as_deg()};
    }

    void set_scale(const v3f &scale) {
        this->m_scale = scale;
        mark_dirty();
    }

private:
    m4f m_local_matrix;
    v3f m_translation = {0, 0, 0};
    anglef m_rotation_x = anglef::zero();
    v3f m_scale = {1, 1, 1};
    anglef m_rotation_y = anglef::zero();
    anglef m_rotation_z = anglef::zero();

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

    geometry_t(RenderState &state) : m_state(state) {
        m_handle = m_state.add_object();
    }

    void set_mesh(MeshHandle mesh) {
        if (m_data.mesh != mesh) {
            m_data.mesh = mesh;
            m_state.assign_geometry(m_handle, mesh);
        }
    }

    void set_material(Material &material) {
        if (m_data.material != material.handle()) {
            m_data.material = material.handle();
            m_state.assign_material(m_handle, material.handle());
            m_state.assign_shader(m_handle, material.shader());
        }
    }

    void set_transform(const m4f &transform) {
        // oh yeah the transform is packed.
        bool equal = true;
        for (int i = 0; i < 12; i++) {
            if (m_data.transform[i] != transform.m[i]) {
                equal = false;
                break;
            }
        }

        if (!equal) {
            memcpy(m_data.transform, transform.m, sizeof(m_data.transform));
            m_state.update_transform(m_handle, transform);
        }
    }

    void accept(node_visitor_t &visitor) override {
        visitor.visit(*this);
    }

    ObjectHandle handle() {
        return m_handle;
    }
private:
    RenderState &m_state;
    ObjectData m_data;
    ObjectHandle m_handle;
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

class scene_t {
public:
    scene_t(RenderState &render_state) : m_render_state(render_state), m_default_material(m_render_state) {
        auto default_shader = m_render_state.load_shader({
            .glsl_vert_path = "shaders/forward.vert",
            .glsl_frag_path = "shaders/forward.frag",
        });

        m_default_material.set_color({1, 1, 1, 1});
        m_default_material.set_roughness(0.5f);
        m_default_material.set_metallic(0.0f);
        m_default_material.set_shader(default_shader);

        m_default_material.set_texture_scale(1.0f);
    }

    void add(node_t *node) {
        m_root.add(node);
    }
    void accept(node_visitor_t &visitor) {
        m_root.accept(visitor);
    }

    void clear();

    // resets the scene to its initial state.
    void reset_to_initial_state();

#define DECL_CREATOR(name, tname, storage) \
    template <typename ...Args> \
    tname *create_##name(Args... args) { \
        auto ptr = storage.alloc_make(args...); \
        return ptr; \
    }

    DECL_CREATOR(group, group_t, storage.groups)
    DECL_CREATOR(directional_light, directional_light_t, storage.directional_lights)
    DECL_CREATOR(transform, transform_t, storage.transforms)
    DECL_CREATOR(camera, camera_t, storage.cameras)
#undef DECL_CREATOR

    point_light_t *create_point_light() {
        auto ptr = new point_light_t(m_render_state);
        return ptr;
    }

    geometry_t *create_geometry(MeshHandle mesh) {
        auto ptr = new geometry_t(m_render_state);
        ptr->set_mesh(mesh);
        ptr->set_material(m_default_material);
        return ptr;
    }

    Material &default_material() {
        return m_default_material;
    }

    Material *create_material() {
        return new Material(m_render_state);
    }

private:
    group_t m_root;

    // @note: this is only some of the data. The important part is that
    //
    struct node_storage_t {
        pool_allocator_t<group_t> groups;
        pool_allocator_t<geometry_t> geometries;
        pool_allocator_t<point_light_t> point_lights;
        pool_allocator_t<directional_light_t> directional_lights;
        pool_allocator_t<transform_t> transforms;
        pool_allocator_t<camera_t> cameras;
    } storage;
    RenderState &m_render_state;

    Material m_default_material;

    bool modified_on_disk = false;
    std::string disk_path;
};

bool load(RenderState &state, const char *path, scene_t &scene);

}
