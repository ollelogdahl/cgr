#import "ieee.typ": *
#show: ieee.with(
  title: [Shader Based Scene-Graph],
  course: [Advanced Computer Graphics, 5DV180],
  authors: (
    (
      name: "Olle Lögdahl",
      email: "olle.logdahl@umu.se"
    ),
  ),
)

#import "@preview/codly:1.2.0": *
#import "@preview/codly-languages:0.1.1": *
#show: codly-init.with()
 
= Introduction

= Requirements

#figure(
  table(columns: (2fr, 1fr),
    table.header([Requirement], [Done]),
    [Possible to build geometry by "hand" API], [Yes],
    [Implemented State class], [Yes],
    [3D File reading support], [Yes (obj and m3d)]
  ),
  caption: "Requirements for the project."
)

= Bonus Requirements

#figure(
  table(columns: (2fr, 1fr),
    table.header([Requirement], [Done]),
    [Possible to build geometry by "hand" using your API], [Yes]
  ),
  caption: "Bonus requirements for the project."
)

= Vulkan Renderer

The engine architecture is divided into multiple layers.

- *GPU Layer* - The gpu layer is responsible for interfacing with the Vulkan API
  and creating the swapchain, creating render passes and the frame preparation and
  submission.
- *Renderer Layer* - The renderer is responsible for rendering geometry. Currently,
  quite simple. In the future this will contain multiple render passes.
- *Scene Layer* - A semantic scene graph which can be traversed for building commands
  to the renderer.

#colbreak()

```cpp
gpu_t gpu;
renderer_t renderer;

gpu.init();
renderer.init(gpu);

while(!windowShouldClose()) {
  renderer.new_frame();

  // application logic
  ...

  renderer.update_frame_data(props);
  gpu.frame([&](gpu_t::frame_t &frame) {
    renderer.draw(frame);
  });

  pollEvents();
}
```

The public API of the renderer is designed around the Command design pattern.
Instead of issuing draw-calls directly, the user creates objects which _instructs_
how the renderer should later draw the scene. This has 2 advantages:
- The renderer may optimize the draw-calls (reorder them by shader or buffers).
- Context about how to render the scene is not needed when geometry is added.

```cpp
class renderer_t {
public:
    void init(gpu_t &, loader_t &);

    texhnd_t define_texture(ref_t<texture_t>);

    void add_draw(const draw_element_t &);
    void add_point_light(const pl_element_t &);

    void set_camera(camera_t &);

    void new_frame();
    void update_frame_data(const props_t &);
    void draw(gpu_t::frame_t &);
};
```

== Scene Graph

== Resource Management