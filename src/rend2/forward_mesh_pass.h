#pragma once

// @todo: make a base MeshPass

struct View {
    m4f projection;
    m4f view;
    v3f position;
};

// @todo: inter-mesh pass dependency tracking!
// dont know how though.
class ForwardMeshPass {
public:
    void render(RenderTarget& target, const View& view);
private:
};
