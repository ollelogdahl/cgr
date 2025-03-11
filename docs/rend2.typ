#import "ieee.typ": *
#show: ieee.with(
  title: [Assignment 2 --- GPU-Driven Culling],
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

The following document describes the design of a GPU-driven culling system for a rendering engine.
The new renderer is designed to reduce the CPU overhead of rendering large scenes by offloading
as much as possible to the GPU. The result is that an entire scene can be rendered with very few
draw calls, meaning that command buffer recording on the CPU is cheaper.

= Usage guide

In the following section, the usage of the program is described. Firstly in @sec:building, the building process is described.

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
variable at runtime.

== System Requirements

Below are the system requirements for running the application. Note that the program
only works on linux curretly.

- Vulkan 1.2 compatible GPU, supporting both the `VK_KHR_dynamic_rendering` and
  `VK_KHR_synchronization2` extensions. The descriptor indexing feature is also
  required. These are generally widely supported.

= Indirect Rendering

The new renderer uses the traditional geometry rasterization pipeline (see @sec:mesh-render),
but dispatches draw-calls from the GPU instead. This allows for
controlling draws from the GPU.
The purpose of this design was to support culling and LOD selection on the GPU, which would reduce
CPU overhead. 

Pipeline changes and state changes are conventionally not supported directly on the GPU. But draw
calls can be produced by using `VkDrawIndexedIndirect`. When submitted, this command reads draw calls
from a GPU buffer, meaning that multiple draw calls can be produced by a single CPU call.
To be able to render all geometry in as few indirect calls as possible, the renderer needs to reduce
state changes between objects. The following strategies were used:
- Using a single vertex and index buffer.
- Using descriptor arrays for textures.
- Partitioning objects into batches with same shader pipeline.

#figure(
  placement: bottom,
  scope: "parent",
  image(width: 80%, "figures/gpu-driven.svg"),
  caption: "Memory Layout Diagram for the GPU scene representation.",
) <fig:gpu-storage>

The only thing requiring a state change between objects is a change in shader program or pipeline layout.
This means that:
- An entire shadow pass can be done in 1 indirect draw call.
- A forward pass can be done in $N$ calls, where $N$ is the number of unique shaders.

To use this system, the drawable instances (plainly called _objects_ going forward) need to be stored on the
gpu. We call this the _object buffer_. Objects are instances which refer to a mesh, a material and carry instance
data like transformation. When the scene changes on the CPU, the neccessary updates need to happen on the GPU as
well. See @fig:gpu-storage for a diagram of the memory layout.

== Object Ordering

Objects are stored in the object buffer. To support multiple different pipelines, we need to partition
the objects into batches. This also requires indirection of object handles, as any object may move
without the holder knowing. See @fig:object-partition. Note that holes may exist in the object buffer
due to deletion of objects, but this is fine as they have no effect on rendering.

#figure(
  placement: top,
  image(width: 50%, "figures/order-insert.svg"),
  caption: "A sequence of swaps required to re-partition the object buffer.",
) <fig:object-partition>

== Culling Compute

When the objects are stored in the correct order in the object buffer, the draw commands can be
generated. The culling shader performs an AABB-frustum test on every object. All objects which
pass get assigned an index in the draw buffer. The lod selection is done using distance only.
Currently, each batch needs its own culling dispatch. This is to reduce the size of the draw buffer.
See @fig:cull for a diagram of the culling compute shader.

#figure(
    placement: top,
    image(width: 80%, "figures/cull.svg"),
    caption: "Culling compute shader without compaction.",
) <fig:cull>


== Compacting the Draw Buffer

After culling, some objects will be removed and cause holes in the draw buffer. These are not drawn,
as their instance count is set to zero. But as the buffer is consumed as draw calls, they will still
create overhead. On a GTX 1070, the overhead of drawing 125k objects all being culled is around
2.97ms (average 23ns per object). This cost grows with the number of objects.

Compaction needs to be handled when inserting the objects into the draw buffer. The solution is inspired by @wihlidal2016 and utilizes workgroup ballot operations and parallel prefix sum.
The algorithm works on subgroups. Firstly, each invocation checks if the object is visible or not.
This value is stored in a ballot for this subgroup. Then a local offset is calculated by counting
the number of bits set in the subgroup ballot up to this invocation. The total number of bits set in
the ballot is calculated. The first invocation is elected and performs a atomic add to the draw buffers
count field, and then broadcast the returned offset (base offset) to the subgroup. Each invocation now
sums the base offset with the local offset to retrieve the final offset in the draw buffer. All visible
objects can now write to their correct position in the draw buffer.
The algorithm is presented in @fig:compaction.

#figure(
  placement: bottom,
  scope: "parent",
  image(width: 80%, "figures/compaction.svg"),
  caption: "Compaction Algorithm in detail.",
) <fig:compaction>

Using this algorithm, the overhead of drawing 125k culled objects is reduced to a static cost of 0.9ms
for a single empty draw indirect call.

== Full Scene Rendering

To summarize, the rendering process of an entire scene is as follows:
- Wait for new objects to be uploaded.
- Bind buffers.
- For each batch:
  - Run the culling compute shader.
  - Bind pipeline.
  - Perform draw indirect.

Due to lack of time shadows are not implemented. But shadowing could easily be done by adding another
cull and draw pass before doing the main forward pass. Noteworthy is that if the shadow pass does not
need to rebind pipelines (if not using vertex-effects), culling and drawing can be done in 1 pass.

= Performance <sec:performance>

The new system using culling can easily render 27'000 dragons on a NVIDIA
RTX A2000 12GB card in 60fps. In the `manydragons.xml` scene, inspecting the metrics shows that:
- A total of 27'000 dragons exist
- They are all rendered in one batch
- About 5'000 are actually drawn
- Approximately 51 million input vertices and 17 million input triangles
- 30 million vertex invocations
- depending on direction, 2-6 million fragment invocations

See @fig:gpu-time for a comparison. Note that the actual GPU command recording on the CPU only takes
120us in the new design. Adapting the old scene graph gives an overhead of 1.9ms just for traversing
the scene graph. This shows clearly that the old scene-graph is not sufficient.

#figure(
  table(columns: (2fr, 1fr, 1fr, 1fr, 1fr, 1fr,), 
    [Dragons], table.vline(), table.cell(colspan: 2, [Old]), table.vline(), table.cell(colspan: 3, [New]),
    [], [CPU], [Draw], [CPU], [Cull], [Draw],
    table.hline(),
    [27000], [2.9ms], [22.2ms], [2.0ms], [30.7us], [9.3ms]
  ),
  caption: [Comparison between the old and the new design, measuring duration for rendering a single frame.],
) <fig:gpu-time>

= Analysis

In the following section, the design is analyzed from different perspectives.

== Manual Vertex Pulling

A problem with the current design is that a batch requires all object to use the same vertex layout.
While not an issue for us, it is a limitation that could be avoided. The whole Input Assembly stage can
be avoided by manually fetching vertex data in the vertex shader. Instead of using a vertex buffer,
the vertices can be passed in a storage buffer, and the batches just store the index into it. Each
batch specifies its own stride.

This should be trivial to implement, and would allow for more flexible rendering.

== A note on Mesh Rendering <sec:mesh-render>

Mesh rendering replaces the old rasterization pipeline, and replaces Input Assembler and Vertex
stage with a compute-like shader. This shader can directly output to the rasterizer.
A feature of mesh shading is that the workgroups emit primitives, meaning that
it is critical for the application to split meshes into smaller parts (64/128 triangles) called
_meshlets_.
Culling can be done cheaper than an AABB frustum test if the meshlets are constructed convexly.

Using mesh rendering instead of the current system would have been simpler and likely more
efficient. We wouldn't need the draw buffer at all. Culling could be implemented on a
meshlet level instead of objects, meaning that the geometry would be more evenly distributed.

Overall, mesh shading would be really interesting to try out.

== Scene Graph & GPU Transform

Transitioning to this new design, the scene-graph makes less sense. In a CPU-driven renderer,
all state resides on the CPU and the scene-graph works well to organize it. In our design
the GPU has ownership of data, and the CPU only updates it. 
The current design reuses the old scene-graph transform system. Geometry nodes now reference an element in the _object buffer_, and therefore also own the transform. To minimize bandwidth this requires logic on the scene-graph side to ensure that transforms are not updated when not changed.

Another possibility would be to keep the entire transform data on the gpu. The current transform tree could be flattened into a linear list. For rendering performance, this could be split into two separate buffers; a _transform node buffer_ for the flattened tree structure, and the _transform buffer_ only containing transforms required when rendering.

This system could easily be extended to GPU bone animations. This could allow GPU-side procedural animation like wind.
Using these buffers would obviously cause some redundant transforms (i.e. transforms not used for rendering but for structure) to consume VRAM. From a design perspective it would be important to not use redundant transforms; but that should already be a consideration. The major issue is that tree traversal is generally not efficient on the GPU. Calculating the global transform requires the parent element world transform, meaning that we can at worst only traverse one element at a time. Due to subgroups this would
reduce the GPU utilization to somewhere around 1/64 or 1/128.

== Resource ownership
The previous system utilized reference counting for tracking GPU resources. In theory, this allows for automatic freeing of resources when a handle is no longer held. This was implemented using C++ constructors/destructors and RAII. While it worked in the previous system where the scene graph owned each node, it would not work for the new design.

Firstly, resources need to be freed at the right time. It is invalid to free a buffer while it is in use by a command list. As we have multiple frames in flight, and thus also multiple lists, freeing a resource needs to be delayed. An advantage of having 
shared buffers is that a model (being a subrange in the buffer) can be freed and replaced in one frame.

Secondly, the resources are now owned by the GPU. Therefore the CPU usually cannot know wether a resource is referenced or not.

Using plain handles gives us two possible freeing strategies:
- Explicit free; change of scene or rooms.
- Automatic free; Textures could be simply replaced using a LRU scheme.

Overall, this design is interesting and very important for a real system. In our case freeing is simply ignored for simplicity. The replacement scheme
can be used for streaming textures for example.

= Requirements
See @fig:requirements for a list of the requirements of the
program. See @fig:bonus-requirements for a list of the bonus requirements
which have been implemented.

#figure(
  placement: top,
  scope: "parent",
  table(columns: (3fr, 2fr),
    table.header([Requirement], [Done]),
    [Ground Plane], [Yes],
    [Moving Objects], [Yes, animations],
    [View Frustum Culling], [Yes],
    [Key to toggle VFC], [No],
    [Frame-rate], [No],
    [Performance Discussion], [Yes, see @sec:performance],
  ),
  caption: "Requirements for the project."
) <fig:requirements>

#figure(
  placement: top,
  scope: "parent",
  table(columns: (2fr, 1fr),
    table.header([Requirement], [Done]),
    [Normal Mapping], [Yes],
  ),
  caption: "Bonus requirements for the project."
) <fig:bonus-requirements>

= Future Work

The current design has some flaws, but also shows great promise. The following are some ideas
of future features that fit particularly well with the current design:

*Particle systems* could easily be supported while also being fully GPU-driven. Draw calls can
be generated directly on the GPU reading from a _particle buffer_. A compute shader updates
this buffer. Even advanced effects like collision can be done, as the GPU could maintain some
collision structures.

*Occlusion culling* can be implemented in multiple
ways, and would be simple as everything already resides on the GPU. One way would be to find the
$n$ biggest occluders, and rendering them in an early pass. Then, the culling shader can query
this Hi-Z buffer to determine if an AABB would be fully occluded.

*Raytracing* requires managing acceleration structures on the GPU. This is a natural extension
of the current design, as the GPU already has ownership of the scene data.

*GPU Mesh Skinning* can be done by using a separate compute shader to transform entire meshes.
Utilizing new buffers like a _joint buffer_ and a _transient vertex buffer_ would make this
possible.

*Clustered Shading* is a technique that resolves the problem of many lights in a forward renderer.
Instead of shading every pixel for every light, the screen is divided into clusters. An early
compute pass assigns lights to clusters only if they overlap. When shading a fragment, only the
lights in the fragments cluster are considered.

*IdTech Shadow Atlas*: IdTech uses a big shadow atlas to store shadowmaps for all lights. Each
light can have a different resolution depending on distance.

#bibliography(style: "ieee", "bib.yml")
