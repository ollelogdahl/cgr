#pragma

class update_visitor_t : public sg::node_visitor_t {
public:
    void visit(geometry_t &geometry) override {
        geometry->update();
    }
    void visit(point_light_t &point_light) override {
        point_light->update();
    }
    void visit(camera_t &camera) override {
        camera->update();
    }
    void visit(group_t &group) override {
        for (auto &child : group->children()) {
            child->accept(*this);
        }
        group->update();
    }
    void visit(transform_t &transform) override {
        for (auto &child : transform->children()) {
            child->accept(*this);
        }
        transform->update();
    }
    void visit(lod_t &lod) override {
        for (auto &child : lod->children()) {
            child->accept(*this);
        }
        lod->update();
    }
};
