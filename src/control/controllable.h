#pragma once

#include "linalg.h"
#include <concepts>

// camera or something else!
template <typename T>
concept Controllable = requires (T t) {
    { t.position() } -> std::same_as<v3f>;
    { t.forward() } -> std::same_as<v3f>;
    { t.up() } -> std::same_as<v3f>;

    { t.set_position(v3f()) };
    { t.set_forward(v3f()) };
};
