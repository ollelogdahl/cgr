#pragma once

#include "oc.h"
#include "linalg.h"

struct camera_t {
    v3f position = {0, 0, 2.0};
    v3f target = {0, 0, 0};

    camera_t() = default;

    camera_t(v3f position, v3f target, m4f projection_matrix) : position(position), target(target), projection_matrix(projection_matrix) {
        view_matrix = m4f::look_at(position, target, {0, 1, 0});
    }

    m4f view_matrix;
    m4f projection_matrix;

    void move_relative(v3f local);

    void rotate(anglef xaxis, anglef yaxis);
};
