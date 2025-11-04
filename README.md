# CGR

Handin for 5DV179, advanced computer graphics at UMU, developed over 3 months. This is a GPU-driven
renderer written in vulkan, implementing indirect rendering and clustered forward shading.

See the reports for the [indirect renderer](docs/a2/rend2.pdf) and the [clustered forward shading](docs/a3/a3.pdf).

# Usage

Build using `make`.

Run simply with `./cgr <path to scene>`.

Using glsl shaders requires `glslc` to compile to spir-v. It will automatically be found on the path. If not found,
you can set the path using the `GLSLC_PATH` environment variable.

```
GLSLC_PATH=/path/to/glslc ./cgr
```
