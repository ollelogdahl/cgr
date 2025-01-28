#pragma once

#include <vector>

#include "resource.h"
#include "linalg.h"

namespace cg {

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

class group_t : public node_t {
public:
    void add(node_t &node);
    void accept(node_visitor_t &visitor);
private:
    std::vector<node_t *> children;
};

class scene_t {
public:
    void add(node_t &node);
    void accept(node_visitor_t &visitor);
private:
    std::vector<node_t *> nodes;
};

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
        return m4f::translate(position)
            * m4f::rotate(rotation_x, v3f{1, 0, 0})
            * m4f::rotate(rotation_y, v3f{0, 1, 0})
            * m4f::rotate(rotation_z, v3f{0, 0, 1})
            * m4f::scale(scale);
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
    ref_t<model_t> model;

    void accept(node_visitor_t &visitor) {
        visitor.visit(*this);
    }
};

}
