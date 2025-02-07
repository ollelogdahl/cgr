#include "sg_load.h"

#include "log.h"
#include <tinyxml2/tinyxml2.h>

sg::node_t *interpret_node(loader_t &loader, sg::scene_t &scene, tinyxml2::XMLElement *elem) {
    if (elem->Name() == std::string("group")) {
        sg::group_t *group = scene.create_group();
        for (tinyxml2::XMLElement *child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
            interpret_node(loader, scene, child);
        }

        return group;
    } else if (elem->Name() == std::string("model")) {
        // model creates multiple geometries and transforms.
        auto path = elem->Attribute("file");

        auto model = loader.load_model({
            .path = path,
        });

        // @todo: lods.

        auto group = scene.create_group();
        for (auto &mesh : model->meshes) {
            auto geometry = scene.create_geometry(mesh->vertex_buffer,
                mesh->lods[0].index_buffer, mesh->lods[0].index_count);
            group->add(geometry);
        }

        return group;
    } else if (elem->Name() == std::string("point_light")) {
        sg::point_light_t *point_light = scene.create_point_light();

        return point_light;
    } else if (elem->Name() == std::string("transform")) {
        sg::transform_t *transform = scene.create_transform();
        for (tinyxml2::XMLElement *child = elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
            interpret_node(loader, scene, child);
        }

        return transform;
    } else {
        g_log.error("unknown node type: {}", elem->Name());
        return nullptr;
    }
}

bool load_scene(loader_t &loader, const char *path, sg::scene_t &scene) {
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
