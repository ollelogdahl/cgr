

# Usage

Run simply with `./cgr`.

Using glsl shaders requires `glslc` to compile to spir-v. It will automatically be found on the path. If not found,
you can set the path using the `GLSLC_PATH` environment variable.

```
GLSLC_PATH=/path/to/glslc ./cgr
```

# Docs

ref_t<gpu_buffer_t> is a reference counted buffer which will be destroyed when
the last reference is released.

ref_t<image_t> is a reference to an image. The image may lie on host or device.

texhnd_t is a handle to a texture. It is reference counted and also enforced to be
loaded on the device and mapped to a texture unit.
