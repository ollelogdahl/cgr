#pragma once

#include "resource.h"

enum class cull_mode_t {
    none,
    front,
    back,
};

class material_t {
public:
    ref_t<gpu_shader_t> shader = nullptr;
    cull_mode_t cull_mode = cull_mode_t::back;

    v4f ambient;
    v4f diffuse;
    v4f specular;
    f32 roughness;
    f32 metallic;

    ref_t<texture_t> tex_albedo0 = nullptr;
    ref_t<texture_t> tex_albedo1 = nullptr;
    ref_t<texture_t> tex_albedo2 = nullptr;

    ref_t<texture_t> tex_normal = nullptr;
    ref_t<texture_t> tex_roughness = nullptr;

    // @todo: vector for additional attributes
};
