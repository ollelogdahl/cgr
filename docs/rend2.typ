#set heading(numbering: "1.1.1")
#set par(justify: true)

= Renderer 2

= Indirect Rendering

In the new renderer, the main passes have been structured around gpu-driven rendering.
The current design uses the traditional geometry rasterization pipeline (see @sec:mesh-render),
but dispatches draw-calls from the GPU instead. This allows for more flexible rendering and
controlling draws from GPU.
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
  placement: top,
  image(width: 50%, "figures/gpu-driven.svg"),
  caption: "Memory Layout Diagram for the GPU scene representation.",
) <fig:gpu-storage>

The only thing requiring a state change between objects is a change in shader program or pipeline layout.
This means that:
- An entire shadow pass can be done in 1 indirect draw call.
- A forward pass can be done in $N$ calls, where $N$ is the number of unique shaders.

To use this system, the drawable instances (plainly called _objects_ going forward) need to be stored on the
gpu. We call this the _object buffer_. Objects are instances which refer to a mesh, a material and carry instance
data like transformation. When the scene changes on the CPU, the neccessary updates need to happen on the GPU as
well.

See figure <fig:gpu-storage> for a diagram of the memory layout.

== Object Ordering



== Culling Compute

#figure(
    placement: top,
    image(width: 40%, "figures/cull.svg"),
    caption: "Culling Compute Shader",
)

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
  placement: top,
  image(width: 80%, "figures/compaction.svg"),
  caption: "Compaction Algorithm",
) <fig:compaction>

Using this algorithm, the overhead of drawing 125k culled objects is reduced to a static cost of 0.9ms
for an empty draw call.

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

#bibliography(style: "ieee", "uni.bib")
