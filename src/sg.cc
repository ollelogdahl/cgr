#include "sg.h"

namespace sg {

void scene_t::clear() {
    // delete all nodes.
    nodes.clear();

    // deallocate nodes.
    storage.groups.clear();
    storage.geometries.clear();
    storage.point_lights.clear();
    storage.transforms.clear();
    storage.cameras.clear();
}

void scene_t::reset_to_initial_state() {
    class reset_visitor_t : public node_visitor_t {
    public:
        void visit(transform_t &transform) override {
            transform.reset_to_initial_state();
            transform.accept_children(*this);
        }
        void visit(camera_t &camera) override {
            camera.reset_to_initial_state();
        }
    };

    // @todo: we will need to other things as well (like calculate bounds)

    reset_visitor_t visitor;
    accept(visitor);
}

// default implementations for a visitor
void node_visitor_t::visit(group_t &group) {
    group.accept_children(*this);
}

void node_visitor_t::visit(transform_t &transform) {
    transform.accept_children(*this);
}

}
