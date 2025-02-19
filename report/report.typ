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
    - `rotate` vector3 - Euler rotation in degrees.
    - `scale` vector3
    - `spin` vector3 - An example of an animation.
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
- *shader-vert-glsl* string - path to the glsl file to use as 
  vertex shader.
- *shader-frag-glsl* string - path to the glsl file to use as
  fragment shader.

The properties are used when rendering. providing *mat-tex-albedo[1-2]* makes the material use
multitexturing (@sec:multitexture)

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

The core of the renderer is based upon the command pattern, which was chosen as it allows
separation of producing instructions from their execution.
Rendering is done using a sequence of components, see @fig:draw-seq. Firstly, the scene is traversed by a
visitor which produce _Commands_. A command is a semantic description of what to do (add light, add geometry, ...). Geometries contain references to resources (textures, shaders, buffers). These commands are 
passed to the planner, which simplifies the submission task and optimizes it.

Firstly, the render planner is capable of optimizing state changes. The commands are sorted
according to a heuristic, which allow shaders (pipelines) and buffers to be bound once and
reused. It also resolves the resource references; a reference to an image is transformed
to a texture id (see @sec:texture-bind). The output of the planner is _Operations_,
which are lower-level commands (`bind_pipeline_op`, `draw_indexed_op`, ...).

#figure(
  placement: bottom,
  scope: "parent",
  image("draw.svg"),
  caption: [The process of rendering a scene.]
) <fig:draw-seq>

= GPU Renderer

The rendering is implemented using Vulkan 1.2.

All draw calls use indexed triangles as their primitive.
Materials are uploaded using push constants as that is cheap. A single global
uniform buffer is used for lights, camera and other settings.
The global uniform buffer is updated every frame, but upon measuring this doesn't seem
to have a big impact on performance.

== Texture binding <sec:texture-bind>

To reduce descriptor set changes during rendering, all textures are provided in an
texture descriptor array. This is not a texture array, which enforces all textures to
have the same dimensions. Instead, this allows binding textures to an index in
the array, which then the materials can refer to.

Currently, texture ids are allocated without ever removing them, which we support
at most 64Ki of. A simple replacement policy could be using a FIFO queue or LRU.

This strategy is usually called _bindless_.

= Design

== Libraries
The following are the 3rd party libraries which are used.

- *{fmt}* - C++20 `std::format` implementation.
- *b_backtrace* - backtraces on crashes.
- *Dear ImGui* - UI.
- *meshoptimizer* - Optimizing meshes and generating lod levels.
- *stb_image* - Image loading.
- *Vulkan Memory Allocator* - Vulkan buffer and image allocation.
- *tinyxml2* - XML parsing.
- *tracy* - Tracy client for profiling.

Only `tinyxml2`, `VMA`, `meshoptimizer` and `stb_image` are
required for the functionality of the program. The others
could be removed on a release build.

== Reload <sec:reload>

Development time is a precious asset, which is why all resources
(non code) can be reloaded at runtime. This is implemented using
file-system watches (`inotify`) and hard-coded dependency graphs.
If the shader `demo.glsl` is loaded and a change occurs, the
system will:
- Recompile the glsl to spir-v.
- Create a new shader handle.
- Update all pipelines using it.

While the current system is primitive, it works well for the
present scenario where dependency relations are simple.

== Scene Graph memory and Instantiation <sec:instantiation>

The scene graph is designed using plain pointers for 
performance reasons. The underlying data for each node type
is stored in its own collection. This has a few performance
implications:
- Nodes of same type are kept close together, which
  can improve performance when rendering due to
  spatial locality.
- The memory usage is reduced as no excessive padding between
  unequal types are required.
- Nodes can be allocated using a simple pool allocator,
  giving extremely cheap $O(1)$ alloc and $O(1)$ free.

Using a block-based pool allocator also gives pointer stability
to the allocated nodes. This means that references to a node is valid as long as the node is alive. Deletion can be implemented using a simple mark-and-sweep scheme.

As the scene is primarily based on pointers, there is nothing 
stopping instancing the same node at multiple times. This is presented, along with the memory layout, in @fig:instance.
There is nothing actually stopping circular references in the
scene, which might be usable in an infinite world scenario. 
Currently this is not supported (as some visitors try to traverse the entire graph).

#figure(
  placement: top,
  scope: "parent",
  image("memory.png"),
  caption: [
    A simple scene graph demonstrating instancing.
    The memory 
  ]
) <fig:instance>

== Level of Detail <sec:lod>

Varying level of detail (LOD) is implemented using the *lod* scene node. The node supports 
multiple children, but only one will render at a given time. This is decided by the 
magnitude of this nodes `center`. `set_center` is usually called by a visitor, which transforms the camera into local space. This way, the distance to camera decides which
quality level is presented.

The levels themselves are currently only generated, but it is a trivial change to support
explicit *lod* nodes in the xml format. Using `meshoptimizer`, the lods are generated with
a given max error. A higher error causes a more drastic reduction in vertices.

All LODs use the same vertex buffer, but with their own index buffer.

== PBR <sec:pbr>

The bundled default shader is based on physical rendering.
At it's core, it is based on the Rendering Equation @eq:rendering which enforces properties that blinn phong doesn't; such as energy conservation.

$
L(x, omega_o) = integral_Omega f_r (x, omega_i, omega_o)
L_i (x, omega_i)(omega_i dot n) d omega_i
$ <eq:rendering>

In our engine we use a BRDF which describes how light reflects
specularly and diffusely from a given surface.

This gives our materials a few properties:
- Albedo - Describes the color of the surface.
- Metallic - Determines if the material is metallic or not.
- Roughness - Describes how rough the surface is.  
  Note that roughness values set in materials are remapped
  such that they are linear.

These properties can both be set as values, but can also source from
a texture.

In the future this might be expanded to more properties (sheen,
anisotropic, etc). See @fig:multitex-pbr for a demo of roughness
maps and PBR rendering.

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
) <fig:multitex-pbr>

== Multi-texture blending <sec:multitexture>

Multi-textures are supported in the engine using vertex
blending. If *mat-tex-albedo[1-2]* is provided, the material
will be refered to as multitexture. This requires the
model to have vertex colors. The texture at each fragment
will be a blend of the 3 albedo channels weighted by the
vertex color. If the color is `rgb(1, 0, 0)` only the `albedo0` texture will be used. `rgb(1, 1, 1)` is also legal,
which causes an equal blend of all textures.
See @fig:multitex-pbr for a demo.

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

== 'Hand' built meshes

Models can be built by hand using the loader api. This could be useful for
procedural meshes.

```cpp
loader_t loader;

auto model = loader.build_proc_model({
  .vertices = { {0, 0, 0}, {1, 0, 0}, {0, 0, 1} },
  .normals = { {1, 0, 0}, {0, 1, 0}, {0, 0, 1} },
  .indices = { 0, 1, 2 },
});
```

= Analysis

In the following section the current design is discussed.

== GPU Resources

I think that using bindless textures was a good choice, and makes it
easy to support streaming. To improve the engine, I would like to make
more parts streamable. Using reference-counted pointers to gpu resources
is a slight problem, firstly because they are costly and secondly because
it makes it explicit to the scene graph which resources need to live. Currently,
all resources which are referenced in the scene graph are alive (loaded), and
loading them needs to happen on the main thread before being used. In big scenes
this might not be possible due to resource constraints and it would be better to
unload some mip-maps or entire meshes.

A valid option could be to merge all vertex buffers and index buffers into
two separate GPU buffers. Allocating into this can be done using a CPU-like
allocator. Firstly, this has some performance implications -- specifically
that buffers no longer need binding. In general, this should be cheaper than
the current solution, but it has not been tested yet.

The same thing as textures could be done with materials; instead of using push-constants
every time the active material changes, all materials can live in a shared buffer
and be referenced to with just an id.

= Requirements <sec:reqs>
See @fig:requirements for a list of the requirements of the
program.

#figure(
  placement: top,
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
) <fig:requirements>

== Bonus Requirements <sec:breqs>
See @fig:bonus-requirements for a list of the bonus requirements
which have been implemented.

#figure(
  placement: top,
  scope: "parent",
  table(columns: (2fr, 1fr),
    table.header([Requirement], [Done]),
    [Interactive animation control of light source], [No],
    [Multi-texturing], [Yes],
    [Transparency], [No],
    [Toon Shading], [No, see @sec:pbr],
  ),
  caption: "Bonus requirements for the project."
) <fig:bonus-requirements>

#bibliography("bib.yml")
