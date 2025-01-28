#include "cg.h"

namespace cg {

void group_t::add(node_t &node) {
    children.push_back(&node);
}
void group_t::accept(node_visitor_t &visitor) {
    visitor.visit(*this);
    for (auto &child : children) {
        child->accept(visitor);
    }
}

void scene_t::add(node_t &node) {
    nodes.push_back(&node);
}
void scene_t::accept(node_visitor_t &visitor) {
    for (auto &node : nodes) {
        node->accept(visitor);
    }
}

}
