# Rend2 - The new renderer

## Design

Every gpu resource is refered to using a handle:
- Material handle
- VertexData handle
- IndexData handle
- Texture handle
- Transform Handle

An object is a thing to be rendered. It has:
- Material handle
- VertexData handle
- IndexData handle
- Transform handle

Objects get transformed to draw calls by a Mesh Pass.

## Mesh Pass

There can be multiple mesh passes. A mesh pass essentially keeps a list of
draw calls which map to objects. Different passes work differently:
- Forward pass: Draw calls are sorted by shader, drawn in 'any' order
- Transparent pass: Draw calls are sorted by depth, drawn back-to-front
- Shadow pass:

Each pass has view-data (see GlobalData bufer), and a list of objects.
If objects have changed we must update some gpu buffers.

The objects are then transformed into batches. A batch is a
VkCmdDrawIndexedIndirectCount command. This is called the batch-buffer
and is owned by the GPU completely.

object {

}

## Notes

_From vkguide:_
  In some engines, they remove vertex attributes from the pipelines entirely, and
  instead grab the vertex data from buffers in the vertex shader. Doing that makes
  it much easier to keep 1 big vertex buffer for all drawcalls in the engine even
  if they use different vertex attribute formats. It also allows some advanced
  unpacking/compression techniques, and it’s the main use case for Mesh Shaders.
