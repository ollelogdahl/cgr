#import "ieee.typ": *
#show: ieee.with(
  title: [Assignment 1 --- Shader Based Scene-Graph],
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

#lorem(50)

== Building and running

The program can be built using make. The following commands are available:

- `make release` - Builds the program in release mode.
- `make memcheck` - Builds the program with ubsan and asan enabled.
- `make clean` - Cleans the build directory.

After building the program, it can be run with `./cgr`. The program requires
`glslc` @glslc to be installed and on `$PATH`, as it is used to compile glsl shaders.
If `glslc` is not found, the path can be specified using the `GLSLC_PATH` environment
variable at runtime.

== System Requirements

Bellow are the system requirements for running the application.

- Vulkan 1.2 compatible GPU, supporting both the `VK_KHR_dynamic_rendering` and
  `VK_KHR_synchronization2` extensions. The descriptor indexing feature is also
  required.

= Project description

Firstly, the requirements are listed (@sec:reqs) and then the bonus requirements (@sec:breqs).

== Requirements <sec:reqs>

#figure(
  placement: bottom,
  scope: "parent",
  table(columns: (3fr, 2fr),
    table.header([Requirement], [Done]),
    [Possible to build geometry by "hand" API], [Yes],
    [Implemented State class], [Yes],
    [3D File reading support], [Yes (obj and m3d)],
    [Key to exit application], [Yes (escape)],
    [Key to reload entire scene], [Yes, See @sec:reload],
    [Key to reset the view], [Yes (space)],
    [Lights], [Yes],
    [Textures], [Yes],
    [Material attributes], [Yes],
    [Instantiation], [Yes, see @sec:instantiation],
    [Movement of camera], [Yes],
    [LOD node], [Yes],
    [Animated nodes], [Yes],
  ),
  caption: "Requirements for the project."
)

== Bonus Requirements <sec:breqs>
#lorem(20)

#figure(
  placement: auto,
  scope: "parent",
  table(columns: (2fr, 1fr),
    table.header([Requirement], [Done]),
    [Interactive animation control of light source], [No],
    [Multi-texturing], [Yes],
    [Transparency], [No],
    [Toon Shading], [No, see @sec:pbr],
  ),
  caption: "Bonus requirements for the project."
)

== Libraries
#lorem(30)

- *{fmt}*
- *b_backtrace*
- *Dear ImGui*
- *meshoptimizer* - used for optimizing meshes and generating lod levels.
- *stb_image*
- *Vulkan Memory Allocator*
- *tinyxml2*

= System Design

== Hot Reload <sec:reload>

== Instantiation <sec:instantiation>

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
sg::scene_t scene;

gpu.init();
renderer.init(gpu);

render_visitor_t render_visitor(renderer);

while(!windowShouldClose()) {
  renderer.new_frame();

  scene.accept(render_visitor);

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

#bibliography("bib.yml")