#include "model.h"
#include "sg.h"

#include "log.h"
#include <tinyxml2/tinyxml2.h>
#include <span>

struct ModelCacheKey {
    std::string path;
    std::vector<LODSetting> lod_settings;
};

bool operator==(const ModelCacheKey &lhs, const ModelCacheKey &rhs) {
    if (lhs.path != rhs.path) return false;
    for (size_t i = 0; i < lhs.lod_settings.size(); i++) {
        if (lhs.lod_settings[i].min_distance != rhs.lod_settings[i].min_distance) return false;
        if (lhs.lod_settings[i].target_error != rhs.lod_settings[i].target_error) return false;
    }
    return true;
}

namespace std {
    template <>
    struct hash<ModelCacheKey> {
        std::size_t operator()(const ModelCacheKey &key) const {
            std::size_t h = 0;
            h ^= std::hash<std::string>()(key.path);
            for (auto &lod : key.lod_settings) {
                h ^= std::hash<f32>()(lod.min_distance);
                h ^= std::hash<f32>()(lod.target_error);
            }
            return h;
        }
    };
}

namespace sg {

typedef std::pair<f32, f32> LodSpec;

// @todo: restructure this!
static std::vector<MeshHandle> load_cache_model(RenderState &render_state, const char *path, std::vector<LODSetting> &&lod_settings);

std::vector<f32> parse_attr_list_f32(const std::string_view &v);
std::vector<LodSpec> parse_attr_autolod_spec(const std::string_view &v);
v3f parse_attr_v3f(const std::string_view &v);
v4f parse_attr_color4(const std::string_view &v);
v3f parse_attr_color3(const std::string_view &v);

sg::Material &parse_material(RenderState &state, sg::scene_t &scene, tinyxml2::XMLElement *elem);

sg::node_t *parse_group(RenderState &state, sg::scene_t &scene, tinyxml2::XMLElement *elem);
sg::node_t *parse_model(RenderState &state, sg::scene_t &scene, tinyxml2::XMLElement *elem);
sg::node_t *parse_point_light(RenderState &state, sg::scene_t &scene, tinyxml2::XMLElement *elem);
sg::node_t *parse_directional_light(RenderState &state, sg::scene_t &scene, tinyxml2::XMLElement *elem);
sg::node_t *parse_transform(RenderState &state, sg::scene_t &scene, tinyxml2::XMLElement *elem);
sg::node_t *parse_grid(RenderState &state, sg::scene_t &scene, tinyxml2::XMLElement *elem);
sg::node_t *parse_camera(RenderState &state, sg::scene_t &scene, tinyxml2::XMLElement *elem);

sg::node_t *interpret_node(RenderState &state, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    if (elem->Name() == std::string("group")) {
        return parse_group(state, scene, elem);
    } else if (elem->Name() == std::string("model")) {
        return parse_model(state, scene, elem);
    } else if (elem->Name() == std::string("point-light")) {
        return parse_point_light(state, scene, elem);
    } else if (elem->Name() == std::string("directional-light")) {
        return parse_directional_light(state, scene, elem);
    } else if (elem->Name() == std::string("transform")) {
        return parse_transform(state, scene, elem);
    } else if (elem->Name() == std::string("grid")) {
        return parse_grid(state, scene, elem);
    } else if (elem->Name() == std::string("camera")) {
        return parse_camera(state, scene, elem);
        return parse_camera(state, scene, elem);
    } else {
        g_log.error("unknown node type: {}", elem->Name());
        return nullptr;
    }
}

bool load(RenderState &render_state, const char *path, sg::scene_t &scene) {
    // @todo: reset the current scene.
    (void)path;
    tinyxml2::XMLDocument doc;
    doc.LoadFile(path);

    if (doc.Error()) {
        g_log.error("failed to load scene: {}", doc.ErrorStr());
        return false;
    }

    tinyxml2::XMLElement *root = doc.FirstChildElement("scene");
    if (!root) {
        g_log.error("scene file does not contain a scene element");
        return false;
    }

    for (tinyxml2::XMLElement *elem = root->FirstChildElement(); elem; elem = elem->NextSiblingElement()) {
        sg::node_t *node = interpret_node(render_state, scene, elem);
        if (node != nullptr)
            scene.add(node);
    }

    return true;
}

sg::node_t *parse_group(RenderState &render_state, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    sg::group_t *group = scene.create_group();

    for (tinyxml2::XMLElement *child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
        auto subnode = interpret_node(render_state, scene, child);

        if (subnode != nullptr)
            group->add(subnode);
    }

    return group;
}

sg::node_t *parse_model(RenderState &render_state, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    // model creates multiple geometries and transforms.
    auto path = elem->Attribute("file");
    auto lod_spec = elem->Attribute("auto-lod");

    auto &material = parse_material(render_state, scene, elem);

    bool use_autolod = lod_spec != nullptr;

    std::vector<LodSpec> lod_ranges;
    if (use_autolod) {
        lod_ranges = parse_attr_autolod_spec(std::string_view(lod_spec));
    }

    std::vector<LODSetting> lod_settings;
    for (auto &range : lod_ranges) {
        lod_settings.push_back({
            .min_distance = range.first,
            .target_error = range.second,
        });
    }

    auto cached_model = load_cache_model(render_state, path, std::move(lod_settings));

    auto group = scene.create_group();

    for (auto &mesh : cached_model) {
        auto geometry = scene.create_geometry(mesh);
        geometry->set_material(material);
        group->add(geometry);
    }

    return group;
}

sg::node_t *parse_point_light(RenderState &render_state, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    sg::point_light_t *point_light = scene.create_point_light();

    auto color_attr = elem->Attribute("color");
    auto position_attr = elem->Attribute("position");
    auto intensity_attr = elem->Attribute("intensity");
    auto falloff_attr = elem->Attribute("falloff");

    if (color_attr) {
        auto color = parse_attr_color3(std::string_view(color_attr)).normalized();
        point_light->set_color(color);
    }

    if (intensity_attr) {
        auto intensity = std::strtof(intensity_attr, nullptr);
        point_light->set_intensity(intensity);
    }

    if (position_attr) {
        auto position = parse_attr_v3f(std::string_view(position_attr));
        point_light->set_position(position);
    }

    if (falloff_attr) {
        auto falloff = parse_attr_list_f32(std::string_view(falloff_attr));
        if (falloff.size() == 2) {
            point_light->set_falloff_linear(falloff[0]);
            point_light->set_falloff_quadratic(falloff[1]);
        }
    }

    return point_light;
}

sg::node_t *parse_directional_light(RenderState &render_state, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    sg::directional_light_t *directional_light = scene.create_directional_light();

    auto color_attr = elem->Attribute("color");
    auto position_attr = elem->Attribute("direction");

    if (color_attr) {
        directional_light->set_color(parse_attr_color3(std::string_view(color_attr)));
    } else {
        directional_light->set_color(v3f{1, 1, 1});
    }

    if (position_attr) {
        directional_light->set_direction(v3f::normalize(parse_attr_v3f(std::string_view(position_attr))));
    } else {
        directional_light->set_direction(v3f{0, -1, 0});
    }

    return directional_light;
}

sg::node_t *parse_transform(RenderState &render_state, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    sg::transform_t *transform = scene.create_transform();

    auto translate_attr = elem->Attribute("translate");
    auto scale_attr = elem->Attribute("scale");
    auto rotate_attr = elem->Attribute("rotate");

    auto spin_attr = elem->Attribute("spin");

    v3f translate = {0, 0, 0};
    v3f scale = {1, 1, 1};
    v3f rotate = {0, 0, 0};
    if (translate_attr != nullptr) {
        translate = parse_attr_v3f(std::string_view(translate_attr));
    }
    if (scale_attr != nullptr) {
        scale = parse_attr_v3f(std::string_view(scale_attr));
    }
    if (rotate_attr != nullptr) {
        rotate = parse_attr_v3f(std::string_view(rotate_attr));
    }

    transform->set_initial_transform(translate, rotate, scale);

    if (spin_attr) {
        auto spin = parse_attr_v3f(std::string_view(spin_attr));
        transform->set_update_callback([spin, transform](sg::node_t &node) {
            transform->set_euler_rotation(transform->euler_rotation() + spin);
        });
    }

    for (tinyxml2::XMLElement *child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
        auto subnode = interpret_node(render_state, scene, child);
        if (subnode != nullptr)
            transform->add(subnode);
    }

    return transform;
}

sg::node_t *parse_grid(RenderState &render_state, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    // grid repeats its children in a grid pattern.
    auto grid = scene.create_group();

    auto count_attr = elem->Attribute("count");
    auto spacing_attr = elem->Attribute("spacing");

    if (!count_attr || !spacing_attr) {
        g_log.error("grid requires count and spacing attributes");
        return nullptr;
    }

    auto count = parse_attr_v3f(std::string_view(count_attr));
    auto spacing = parse_attr_v3f(std::string_view(spacing_attr));

    for (f32 x = 0; x < count.x; x++) {
        auto halfx = (count.x - 1) * spacing.x / 2;
        for (f32 y = 0; y < count.y; y++) {
            auto halfy = (count.y - 1) * spacing.y / 2;
            for (f32 z = 0; z < count.z; z++) {
                auto halfz = (count.z - 1) * spacing.z / 2;

                auto transform = scene.create_transform();

                v3f translate = {x * spacing.x - halfx, y * spacing.y - halfy, z * spacing.z - halfz};
                transform->set_initial_transform(translate, v3f{0, 0, 0}, v3f{1, 1, 1});

                // @todo: this is soo bad, but we need to copy the children.
                std::vector<sg::node_t *> children;
                for (tinyxml2::XMLElement *child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
                    auto subnode = interpret_node(render_state, scene, child);
                    if (subnode != nullptr)
                        children.push_back(subnode);
                }
                for (auto &child : children) {
                    transform->add(child);
                }

                grid->add(transform);
            }
        }
    }

    return grid;
}

sg::node_t *parse_camera(RenderState &render_state, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    auto camera = scene.create_camera();

    auto position_attr = elem->Attribute("position");
    auto target_attr = elem->Attribute("target");
    auto up_attr = elem->Attribute("up");

    auto is_controlled = elem->Attribute("controlled") != nullptr;

    v3f position = {0, 0, 0};
    v3f target;
    v3f up = {0, 1, 0};
    if (position_attr) {
        position = parse_attr_v3f(std::string_view(position_attr));
    }
    if (target_attr) {
        target = parse_attr_v3f(std::string_view(target_attr));
    } else {
        target = position - v3f{0, 0, 1};
    }

    if (up_attr) {
        up = parse_attr_v3f(std::string_view(up_attr));
    }

    camera->set_initial_state(position, target, up);

    camera->set_controlled(true);

    // @todo: different projections.
    // @todo: aspect ratio ?????
    // camera->initial_perspective(anglef::from_deg(80.0f), 1.0f, 0.1f, 100.0f);

    return camera;
}

std::vector<f32> parse_attr_list_f32(const std::string_view &v) {
    // parse "<num>:<num>:<num>:..."
    std::vector<f32> result;
    std::string_view it = v;
    std::string delimiter = ":";
    size_t pos = 0;
    while ((pos = it.find(delimiter)) != std::string::npos) {
        std::string_view token = it.substr(0, pos);
        char *end;
        f32 value = std::strtof(token.data(), &end);
        result.push_back(value);
        it = it.substr(pos + delimiter.length());
    }
    char *end;
    f32 value = std::strtof(it.data(), &end);
    result.push_back(value);

    return result;
}

std::vector<LodSpec> parse_attr_autolod_spec(const std::string_view &v) {
    // "<distance>,<error>:<distance>,<error>:..."
    std::vector<LodSpec> result;
    std::string_view it = v;
    std::string delimiter = ":";
    size_t pos = 0;
    while ((pos = it.find(delimiter)) != std::string::npos) {
        std::string_view pair = it.substr(0, pos);
        std::string_view distance = pair.substr(0, pair.find(","));
        std::string_view error = pair.substr(pair.find(",") + 1);
        char *end;
        float d = std::strtof(distance.data(), &end);
        float e = std::strtof(error.data(), &end);
        result.push_back({d, e});
        it = it.substr(pos + delimiter.length());
    }

    std::string_view pair = it;
    std::string_view distance = pair.substr(0, pair.find(","));
    std::string_view error = pair.substr(pair.find(",") + 1);

    char *end;
    float d = std::strtof(distance.data(), &end);
    float e = std::strtof(error.data(), &end);
    result.push_back({d, e});

    return result;
}

v3f parse_attr_v3f(const std::string_view &v) {
    // parse a string "x y z"
    v3f result;
    sscanf(v.data(), "%f %f %f", &result.x, &result.y, &result.z);

    return result;
}

v4f parse_attr_color4(const std::string_view &v) {
    // for now, just parse a v4f. In the future, also hex colors may be supported.

    v4f result;
    auto found = sscanf(v.data(), "%f %f %f %f", &result.x, &result.y, &result.z, &result.w);
    if (found == 3) {
        result.w = 1.0f;
    }

    return result;
}

v3f parse_attr_color3(const std::string_view &v) {
    v3f result;
    sscanf(v.data(), "%f %f %f", &result.x, &result.y, &result.z);

    return result;
}

sg::Material &parse_material(RenderState &state, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    // read the element, and pick out state attributes.

    bool any_material_attr_set = false;

    auto mat_diffuse_attr = elem->Attribute("mat-color");
    auto mat_roughness_attr = elem->Attribute("mat-roughness");
    auto mat_metallic_attr = elem->Attribute("mat-metallic");

    auto mat_tex_scale_attr = elem->Attribute("mat-tex-scale");

    auto mat_tex_albedo0_attr = elem->Attribute("mat-tex-albedo0");
    auto mat_tex_albedo1_attr = elem->Attribute("mat-tex-albedo1");
    auto mat_tex_albedo2_attr = elem->Attribute("mat-tex-albedo2");

    auto mat_tex_normal_attr = elem->Attribute("mat-tex-normal");
    auto mat_tex_metallic_attr = elem->Attribute("mat-tex-metallic");
    auto mat_tex_roughness_attr = elem->Attribute("mat-tex-roughness");

    auto shader_glsl_frag_attr = elem->Attribute("shader-frag-glsl");
    auto shader_glsl_vert_attr = elem->Attribute("shader-vert-glsl");

    any_material_attr_set |= mat_diffuse_attr != nullptr;
    any_material_attr_set |= mat_roughness_attr != nullptr;
    any_material_attr_set |= mat_metallic_attr != nullptr;
    any_material_attr_set |= mat_tex_scale_attr != nullptr;
    any_material_attr_set |= mat_tex_albedo0_attr != nullptr;
    any_material_attr_set |= mat_tex_albedo1_attr != nullptr;
    any_material_attr_set |= mat_tex_albedo2_attr != nullptr;
    any_material_attr_set |= mat_tex_normal_attr != nullptr;
    any_material_attr_set |= mat_tex_metallic_attr != nullptr;
    any_material_attr_set |= mat_tex_roughness_attr != nullptr;

    any_material_attr_set |= shader_glsl_frag_attr != nullptr;
    any_material_attr_set |= shader_glsl_vert_attr != nullptr;

    if (any_material_attr_set) {
        auto material = scene.create_material();
        material->set_color(scene.default_material().color());
        material->set_emission(scene.default_material().emission());
        material->set_roughness(scene.default_material().roughness());
        material->set_metallic(scene.default_material().metallic());
        material->set_texture_scale(1.0f);

        if (mat_diffuse_attr) {
            material->set_color(parse_attr_color4(std::string_view(mat_diffuse_attr)));
        }
        if (mat_roughness_attr) {
            material->set_roughness(std::strtof(mat_roughness_attr, nullptr));
        }
        if (mat_metallic_attr) {
            material->set_metallic(std::strtof(mat_metallic_attr, nullptr));
        }

        if (mat_tex_scale_attr) {
            material->set_texture_scale(std::strtof(mat_tex_scale_attr, nullptr));
        }

        if (mat_tex_albedo0_attr) {
            auto tex = state.load_texture({.path = mat_tex_albedo0_attr, .type = TextureType::RGBA});
            material->set_albedo0(tex);
        }

        if (mat_tex_albedo1_attr) {
            auto tex = state.load_texture({.path = mat_tex_albedo1_attr, .type = TextureType::RGBA});
            material->set_albedo1(tex);
        }

        if (mat_tex_albedo2_attr) {
            auto tex = state.load_texture({.path = mat_tex_albedo2_attr, .type = TextureType::RGBA});
            material->set_albedo2(tex);
        }

        if (mat_tex_normal_attr) {
            auto tex = state.load_texture({.path = mat_tex_normal_attr, .type = TextureType::RGBA});
            material->set_normal(tex);
        }

        if (mat_tex_metallic_attr) {
            auto tex = state.load_texture({.path = mat_tex_metallic_attr, .type = TextureType::R});
            material->set_metallic(tex);
        }

        if (mat_tex_roughness_attr) {
            auto tex = state.load_texture({.path = mat_tex_roughness_attr, .type = TextureType::R});
            material->set_roughness(tex);
        }

        if (shader_glsl_frag_attr && shader_glsl_vert_attr) {
            material->set_shader(state.load_shader({
                .glsl_vert_path = shader_glsl_vert_attr,
                .glsl_frag_path = shader_glsl_frag_attr,
            }));
        }

        return *material;
    }
    return scene.default_material();

    /*
    if (mat_tex_albedo0_attr) {
        success = true;
        state.material->tex_albedo0 = render_state.load_texture({.path = mat_tex_albedo0_attr});
    }
    if (mat_tex_albedo1_attr) {
        success = true;
        state.material->tex_albedo1 = render_state.load_texture({.path = mat_tex_albedo1_attr});
    }
    if (mat_tex_albedo2_attr) {
        success = true;
        state.material->tex_albedo2 = render_state.load_texture({.path = mat_tex_albedo2_attr});
    }
    if (mat_tex_normal_attr) {
        success = true;
        state.material->tex_normal = render_state.load_texture({.path = mat_tex_normal_attr, .srgb = false});
    }
    if (mat_tex_roughness_attr) {
        success = true;
        state.material->tex_roughness = render_state.load_texture({.path = mat_tex_roughness_attr, .srgb = false});
    }

    if (shader_glsl_frag_attr && shader_glsl_vert_attr) {
        success = true;
        state.material->shader = render_state.load_shader_program({
            .vertex_glsl_path = shader_glsl_vert_attr,
            .fragment_glsl_path = shader_glsl_frag_attr,
        });
    }
    */
}

std::unordered_map<ModelCacheKey, std::vector<MeshHandle>> model_cache;

std::vector<MeshHandle> load_cache_model(RenderState &render_state, const char *path, std::vector<LODSetting> &&lod_settings) {
    auto key = ModelCacheKey{path, lod_settings};
    auto it = model_cache.find(key);
    if (it != model_cache.end()) {
        return it->second;
    }

    auto model = load_model(path, lod_settings);
    std::vector<MeshHandle> handles;
    for (auto &mesh : model.meshes) {
        auto handle = render_state.add_mesh(mesh);
        handles.push_back(handle);
    }

    model_cache[key] = handles;
    return handles;
}

}
