#include "sg.h"

#include "resource.h"
#include "log.h"
#include <tinyxml2/tinyxml2.h>

namespace sg {

typedef std::pair<f32, f32> LodSpec;

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

sg::node_t *interpret_node(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    if (elem->Name() == std::string("group")) {
        sg::group_t *group = scene.create_group();
        for (tinyxml2::XMLElement *child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
            auto subnode = interpret_node(loader, scene, child);

            if (subnode != nullptr)
                group->add(subnode);
        }

        return group;
    } else if (elem->Name() == std::string("model")) {
        // model creates multiple geometries and transforms.
        auto path = elem->Attribute("file");
        auto lod_spec = elem->Attribute("auto-lod");

        bool use_autolod = lod_spec != nullptr;

        std::vector<LodSpec> lod_ranges;
        if (use_autolod) {
            lod_ranges = parse_attr_autolod_spec(std::string_view(lod_spec));
        }

        std::vector<model_load_params_t::lod_setting_t> lod_settings;
        for (auto &range : lod_ranges) {
            lod_settings.push_back({
                .error_limit = range.second,
                .sloppy = true,
            });
        }

        auto model = loader.load_model({
            .path = path,
            .lod_settings = {lod_settings.data(), lod_settings.size()},
        });

        if (use_autolod) {
            auto lod = scene.create_lod();


            // add the default lod.
            auto group0 = scene.create_group();
            for (auto &mesh : model->meshes) {
                fmt::println("gen lods: {}", mesh->lods.size());
                auto geometry = scene.create_geometry(mesh->vertex_buffer,
                    mesh->lods[0].index_buffer, mesh->lods[0].index_count);
                group0->add(geometry);
            }
            lod->add(group0);

            std::vector<f32> min_ranges = {};
            min_ranges.push_back(0.0f);

            for (usize i = 0; i < lod_ranges.size(); i++) {
                auto &range = lod_ranges[i];
                min_ranges.push_back(range.first);

                auto group = scene.create_group();
                for (auto &mesh : model->meshes) {
                    auto geometry = scene.create_geometry(mesh->vertex_buffer,
                        mesh->lods[i + 1].index_buffer, mesh->lods[i + 1].index_count);
                    group->add(geometry);
                }
                lod->add(group);
            }
            lod->set_ranges(min_ranges);

            return lod;
        } else {
            auto group = scene.create_group();
            for (auto &mesh : model->meshes) {
                auto geometry = scene.create_geometry(mesh->vertex_buffer,
                    mesh->lods[0].index_buffer, mesh->lods[0].index_count);
                group->add(geometry);
            }

            return group;
        }
    } else if (elem->Name() == std::string("point_light")) {
        sg::point_light_t *point_light = scene.create_point_light();

        return point_light;
    } else if (elem->Name() == std::string("transform")) {
        sg::transform_t *transform = scene.create_transform();

        auto scale = elem->Attribute("scale");
        if (scale != nullptr) {
            transform->scale = parse_attr_v3f(std::string_view(scale));
        }

        for (tinyxml2::XMLElement *child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
            auto subnode = interpret_node(loader, scene, child);
            if (subnode != nullptr)
                transform->add(subnode);
        }

        return transform;
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

}
