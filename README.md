

# Usage

Build using `make`.

Run simply with `./cgr <path to scene>`.

Using glsl shaders requires `glslc` to compile to spir-v. It will automatically be found on the path. If not found,
you can set the path using the `GLSLC_PATH` environment variable.

```
GLSLC_PATH=/path/to/glslc ./cgr
```

An installation of glslc is bundled, but will most likely not work (works on the lab machines)