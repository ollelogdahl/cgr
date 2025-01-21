#include "camera.h"

void camera_t::move_relative(v3f local) {
    v3f direction = target - position;
    v3f up = {0, 1, 0};
    v3f right = v3f::cross(direction, up);
    v3f new_target = target + right * local.x + up * local.y + direction * local.z;
    v3f new_position = position + right * local.x + up * local.y + direction * local.z;

    target = new_target;
    position = new_position;

    view_matrix = m4f::look_at(position, target, up);
}

void camera_t::rotate(anglef xaxis, anglef yaxis) {
    v3f direction = target - position;
    v3f up = {0, 1, 0};
    v3f right = v3f::cross(direction, up);
    m4f rotation = m4f::rotate(xaxis, right) * m4f::rotate(yaxis, up);

    // update the direction
    v4f d = direction.to_homogeneous() * rotation;
    target = position + d.xyz();

    // update view matrix
    view_matrix = m4f::look_at(position, target, up);
}
