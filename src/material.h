#pragma once

#include "resource.h"

class material_t {
public:
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
};
