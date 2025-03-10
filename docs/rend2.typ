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

== Scene Graph

Transitioning to this new design, the scene-graph makes less sense. In a CPU-driven renderer,
all state resides on the CPU and the scene-graph works well to organize it. In our design
the GPU has ownership of data, and the CPU only updates it. 

== Future Work

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

#bibliography(style: "ieee", "uni.bib")
