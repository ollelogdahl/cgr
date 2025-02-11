#include "sg.h"

#include "resource.h"
#include "log.h"
#include <tinyxml2/tinyxml2.h>

namespace sg {

typedef std::pair<f32, f32> LodSpec;

std::vector<f32> parse_attr_list_f32(const std::string_view &v);
std::vector<LodSpec> parse_attr_autolod_spec(const std::string_view &v);
v3f parse_attr_v3f(const std::string_view &v);
v4f parse_attr_color4(const std::string_view &v);
v3f parse_attr_color3(const std::string_view &v);

sg::state_t parse_state(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem, bool &success);

sg::node_t *parse_group(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem);
sg::node_t *parse_model(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem);
sg::node_t *parse_point_light(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem);
sg::node_t *parse_transform(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem);
sg::node_t *parse_lod(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem);

sg::node_t *interpret_node(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    if (elem->Name() == std::string("group")) {
        return parse_group(loader, scene, elem);
    } else if (elem->Name() == std::string("model")) {
        return parse_model(loader, scene, elem);
    } else if (elem->Name() == std::string("point_light")) {
        return parse_point_light(loader, scene, elem);
    } else if (elem->Name() == std::string("transform")) {
        return parse_transform(loader, scene, elem);
    } else if (elem->Name() == std::string("lod")) {
        return parse_lod(loader, scene, elem);
    } else {
        g_log.error("unknown node type: {}", elem->Name());
        return nullptr;
    }
}

bool load(loader_t &loader, const char *path, sg::scene_t &scene) {
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
        sg::node_t *node = interpret_node(loader, scene, elem);
        scene.add(node);
    }

    return true;
}

sg::node_t *parse_group(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    sg::group_t *group = scene.create_group();

    bool set_state;
    sg::state_t state = parse_state(loader, scene, elem, set_state);
    if (set_state) {
        auto sref = scene.create_state(state);
        group->set_state(sref);
    }

    for (tinyxml2::XMLElement *child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
        auto subnode = interpret_node(loader, scene, child);

        if (subnode != nullptr)
            group->add(subnode);
    }

    return group;
}

sg::node_t *parse_model(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    // model creates multiple geometries and transforms.
    auto path = elem->Attribute("file");
    auto lod_spec = elem->Attribute("auto-lod");

    bool set_state;
    sg::state_t state = parse_state(loader, scene, elem, set_state);
    sg::state_t *state_ptr = nullptr;
    if (set_state) {
        state_ptr = scene.create_state(state);
    }

    bool use_autolod = lod_spec != nullptr;

    std::vector<LodSpec> lod_ranges;
    if (use_autolod) {
        lod_ranges = parse_attr_autolod_spec(std::string_view(lod_spec));
    }

    std::vector<model_load_params_t::lod_setting_t> lod_settings;
    for (auto &range : lod_ranges) {
        lod_settings.push_back({
            .error_limit = range.second,
        });
    }

    auto model_desc = loader.load_model({
        .path = path,
        .lod_settings = std::move(lod_settings),
    });

    if (use_autolod) {
        auto lod = scene.create_lod();
        lod->set_state(state_ptr);

        std::vector<f32> min_ranges = {};
        min_ranges.reserve(lod_ranges.size() + 1);

        min_ranges.push_back(0.0f);
        for (usize i = 0; i < lod_ranges.size() + 1; i++) {
            if (i > 0) {
                min_ranges.push_back(lod_ranges[i - 1].first);
            }

            auto group = scene.create_group();
            group->set_state(state_ptr);

            for (auto &mesh : model_desc.meshes) {
                auto geometry = scene.create_geometry(mesh.vertex_buffer,
                    mesh.lods[i].index_buffer, mesh.lods[i].index_count);

                geometry->set_state(state_ptr);
                group->add(geometry);
            }

            lod->add(group);
        }
        lod->set_ranges(min_ranges);

        return lod;
    } else {
        auto group = scene.create_group();
        group->set_state(state_ptr);

        for (auto &mesh : model_desc.meshes) {
            auto geometry = scene.create_geometry(mesh.vertex_buffer,
                mesh.lods[0].index_buffer, mesh.lods[0].index_count);

            geometry->set_state(state_ptr);
            group->add(geometry);
        }

        return group;
    }
}

sg::node_t *parse_point_light(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    sg::point_light_t *point_light = scene.create_point_light();

    bool set_state;
    sg::state_t state = parse_state(loader, scene, elem, set_state);
    if (set_state) {
        auto sref = scene.create_state(state);
        point_light->set_state(sref);
    }

    auto color_attr = elem->Attribute("color");
    auto position_attr = elem->Attribute("position");

    auto position = parse_attr_v3f(std::string_view(position_attr));
    auto color = parse_attr_color3(std::string_view(color_attr));

    point_light->position = position;
    point_light->color = color;

    return point_light;
}

sg::node_t *parse_transform(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    sg::transform_t *transform = scene.create_transform();

    bool set_state;
    sg::state_t state = parse_state(loader, scene, elem, set_state);
    if (set_state) {
        auto sref = scene.create_state(state);
        transform->set_state(sref);
    }

    auto position = elem->Attribute("position");
    auto scale = elem->Attribute("scale");

    if (position != nullptr) {
        transform->position = parse_attr_v3f(std::string_view(position));
    }
    if (scale != nullptr) {
        transform->scale = parse_attr_v3f(std::string_view(scale));
    }

    for (tinyxml2::XMLElement *child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
        auto subnode = interpret_node(loader, scene, child);
        if (subnode != nullptr)
            transform->add(subnode);
    }

    return transform;
}

sg::node_t *parse_lod(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    auto lod = scene.create_lod();

    bool set_state;
    sg::state_t state = parse_state(loader, scene, elem, set_state);
    if (set_state) {
        auto sref = scene.create_state(state);
        lod->set_state(sref);
    }

    auto ranges_attr = elem->Attribute("ranges");

    std::vector<f32> ranges = parse_attr_list_f32(std::string_view(ranges_attr));

    usize num_children = 0;
    for (tinyxml2::XMLElement *child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
        auto subnode = interpret_node(loader, scene, child);
        if (subnode != nullptr) {
            lod->add(subnode);
            num_children++;
        }
    }

    if (num_children != ranges.size()) {
        g_log.error("number of candidate children does not match number of ranges");
        return nullptr;
    }
    lod->set_ranges(ranges);

    return lod;
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

sg::state_t parse_state(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem, bool &success) {
    // read the element, and pick out state attributes.
    success = false;
    state_t state;

    auto mat_diffuse_attr = elem->Attribute("mat-diffuse");
    auto mat_specular_attr = elem->Attribute("mat-specular");
    auto mat_ambient_attr = elem->Attribute("mat-ambient");
    auto mat_roughness_attr = elem->Attribute("mat-roughness");

    if (mat_diffuse_attr) {
        success = true;
        state.material.diffuse = parse_attr_color4(std::string_view(mat_diffuse_attr));
    }
    if (mat_specular_attr) {
        success = true;
        state.material.specular = parse_attr_color4(std::string_view(mat_specular_attr));
    }
    if (mat_ambient_attr) {
        success = true;
        state.material.ambient = parse_attr_color4(std::string_view(mat_ambient_attr));
    }
    if (mat_roughness_attr) {
        success = true;
        state.material.roughness = std::strtof(mat_roughness_attr, nullptr);
    }

    return state;
}

}
