#pragma once

#include "sg.h"

class compute_bounds_visitor_t : public sg::node_visitor_t {
public:
    void visit(sg::group_t &group) override;
    void visit(sg::transform_t &transform) override;
};
