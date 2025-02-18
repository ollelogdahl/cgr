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
  paper-size: "a4",
)


= Introduction

Designing ...

This document outlines an implementation of a scene-graph-based realtime renderer
written in C++.

= Usage guide

In the following section, the usage of the program is described. Firstly in @sec:building, the building process is described.
In @sec:xml, the XML scene format is described.

== Building and running <sec:building>

For building the application, the following are required:
- C++20 / C++2a compatible compiler.
- `GLFW` installed system-wide.
- `make`

The following `make` commands are available:

- `make dev` (default) - Builds the program in dev mode. Full optimizations but with
  tracing enabled.
- `make release` - Builds the program in release mode.
- `make memcheck` - Builds the program with ubsan, asan and validation layers enabled.
- `make clean` - Cleans the build directory.

After building the program, it can be run with `./cgr <scene file>`. The program requires
`glslc` @glslc to be installed and on `$PATH`, as it is used to compile glsl shaders.
If `glslc` is not found, the path can be specified using the `GLSLC_PATH` environment
variable at runtime (see also @sec:xml-shaders).

== System Requirements

Below are the system requirements for running the application. Note that the program
only works on linux curretly.

- Vulkan 1.2 compatible GPU, supporting both the `VK_KHR_dynamic_rendering` and
  `VK_KHR_synchronization2` extensions. The descriptor indexing feature is also
  required. These are generally widely supported.

== XML Scene Format <sec:xml>

The program uses XML files for describing scenes. In the following section, a brief
guide over the syntax is provided.

=== Quickstart

```xml
<scene>
  <camera position="2 0 0" />
  <directional-light />
  <transform scale="0.5 0.5 0.5">
    <model file="cow.obj" />
  </transform>
</scene>
```

=== Nodes

The following are the nodes provided in this first version.

- *group* - A node which contains other nodes.
- *camera* - A camera which will be rendered to screen. Only one should be in the scene. Declare it early.
    - `position` vector3 - Where the camera is located.
    - `forward` vector3 - The direction to look in.
- *tranform* - A node which can contain other nodes and applies an affine transform on them.
    - `translate` vector3
    - `rotate` vector3
    - `scale` vector3
- *model* - A node which contains a mesh and material. May produce multiple geometry nodes with transforms.
    - `file` string - The path to the model file to load.
    - `auto-lod` list - Automatically generate LOD meshes for the model. The format is `distance,error:distance,error:...`.
- *grid* - A demo of node instantiation. Instantiates all children multiple times in a 3D grid.
    - `count` vector3 - The number of repeated instantiations in each dimension.
    - `spacing` vector3 - The distance between each instantiation in each dimension.
- *point-light* - A point light.
    - `position` vector3 - Position relative to the current transform.
    - `color` vector3 - The color specified as rgb (can be higher than 1.0).
- *directional-light* - A directional light (sun).
    - `direction` vector3 - The direction which the sunlight comes from. Automatically normalized.
    - `color` vector3 - The color of the light.

=== States

All nodes support additional state. These are provided as additional attributes on the xml nodes.

- *mat-diffuse* vector4 - a 4-component color (rgba).
- *mat-specular* vector4 - a 4-component color (rgba).
- *mat-ambient* vector4 - a 4-component color (rgba).
- *mat-roughness* float - a single value.
- *mat-tex-albedo0* string - path to a texture.
- *mat-tex-albedo1* string - path to a texture.
- *mat-tex-albedo2* string - path to a texture.
- *mat-tex-normal* string - path to a texture.
- *mat-tex-roughness* string - path to a texture.

The properties are used when rendering. providing *mat-tex-albedo[1-2]* makes the material use
multitexturing.

=== Shaders <sec:xml-shaders>

Custom shaders can be set. Refer to the sample `test.frag` and `test.vert` to see which properties
are always pushed. Custom uniforms are currently not supported.

- *shader-frag-glsl*
- *shader-vert-glsl*
- *shader-frag-spv*
- *shader-vert-spv*

= System Design

The engine architecture is divided into multiple layers.

- *GPU Layer* - The gpu layer is responsible for interfacing with the graphics backend API
  and creating the swapchain, creating render passes and frame preparation and
  submission.
- *Renderer Layer* - The renderer is responsible for rendering geometry.
- *Scene Layer* - A semantic scene graph which can be traversed for building commands
  to the renderer.

#figure(
  placement: bottom,
  scope: "parent",
  image("1.svg"),
  caption: [....]
)

= GPU Backend

The rendering is implemented using Vulkan. 

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
    [Key to reset the view], [Yes (R)],
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

== Reload <sec:reload>

== Instantiation <sec:instantiation>

== PBR <sec:pbr>

#figure(
  placement: bottom,
  scope: "parent",
  grid(columns: 3, gutter: 1em,
    image("man.png"),
    image("manybugs.png"),
  ),
  caption: [
    _Left_: A scene with multiple shaders.
    _Right_: 8000 instances of a bug model running
      in real time.
  ]
)

#figure(
  placement: bottom,
  scope: "parent",
  grid(columns: 3, gutter: 1em,
    image("multitex.png"),
    image("pbr.png"),
  ),
  caption: [
    _Left_: A scene with multitexture blending.
    _Right_: Demonstration of normal-mapping and
    roughness maps in a PBR shader.
  ]
)

#bibliography("bib.yml")
