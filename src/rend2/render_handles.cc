#include "render_handles.h"

std::string format_as(VertexDataHandle h) {
    return fmt::format("vtx:{}", h.idx);
}

std::string format_as(IndexDataHandle h) {
    return fmt::format("idx:{}", h.idx);
}

std::string format_as(MaterialHandle h) {
    return fmt::format("mtl:{}", h.id);
}

std::string format_as(TextureHandle h) {
    return fmt::format("tex:{}", h.id);
}

std::string format_as(MeshHandle h) {
    return fmt::format("msh:{}", h.id);
}

std::string format_as(ObjectHandle h) {
    return fmt::format("obj:{}", h.id);
}
