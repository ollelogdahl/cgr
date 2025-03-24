#import "ieee.typ": *
#show: ieee.with(
  title: [Assignment 3 --- Clustered Forward Shading],
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

The following document describes the design of a clustered forward shading renderer, backed by
GPU-driven rendering. The new renderer supports many lights (up to 10'000 in real time).

= Usage guide

In the following section, the usage of the program is described. Firstly in @sec:building, the building process is described.

== Building and running <sec:building>

For building the application, the following are required:
- C++20 / C++2a compatible compiler.
- `GLFW` installed system-wide.
- `make` or `cmake` (and a buildsystem like `make` or `ninja`)

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

The project can also be built using cmake:

- `cmake -B build -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build --parallel`

== System Requirements

Below are the system requirements for running the application. Note that the program
only works on linux curretly.

- Vulkan 1.3 compatible GPU. The descriptor indexing feature is also
  required. These are generally widely supported.

= Background

A classical forward renderer suffers from some issues, the biggest usually being
overdraw. An overdrawn fragment is both wasted work, as well as inefficient usage
of fill rate. As each fragment is being rendered in a forward-fashion, costly
lighting calcuation occurs for fragments which will not be seen.
Historically, deferred rendering has been proposed to solve this. By rendering only
albedo, normals and other properties per-object, and later shading by pixel,
the shading is only performed on every visible fragment. While this solves overdraw,
deferred shading comes with other issues relating to bandwidth as well as anti-aliasing.

The issue of overdraw can be handled in many ways, like performing an early depth-only
pass or by triangle reordering. @han2016triangle In this assignment we kept using a
forward renderer, but focused on shading optimizations.

Our solution successfully renders a scene with 10'000 point lights in real time.

= Clustered Forward Shading

Clustered shading is a method to reduce lighting calculations by reducing the number
of lights that are tested against each fragment. @olsson2012clustered The method works
by splitting the view frustum into frustum shaped boxes (_froxels_) called clusters and
assigning the lights to the clusters based on their influence. When shading, each fragment
can look up the containing cluster and only calculate shading using those lights. This means
that clusters with no lights perform no lighting calculations.

In our implementation, we define the clusters using axis-aligned bounding boxes in view space.
This is not completely accurate, as there will be slight overlap, but this doesn't seem to cause
any artifacts in our scenes.

A great feature of clustering is that the system can be reused for other spatial-related
systems like decals and light probes.@devilisinthedetails

== Cluster Assignment

The first step is to assign lights to clusters. As we already store all point lights in a buffer,
we use an indirection buffer similar to @devilisinthedetails. Each cluster reference its start of
the list 


== Forward Shading

== Optimized Scalar Reads



= Limitations

== Reflections

The point lights influence is calculated using the range of diffuse lighting. Specular lighting
has no upper range, and would require an influence range of infinity. This means that normal
specular highlights no longer work. This means that reflections need to be handled in another way.

Luckily, reflections calculated using the lights is already very limited. Metallic surfaces usually
become very dark, as there is rarely anything to reflect. This means that most engines already use
another method for reflections. We suggest the following methods:
- Ray-traced reflections
- Screen space reflections
- IBL Cubemap reflections

Each method has its own advantages and disadvantages. Ray-traced reflections are the most accurate,
but also likely the most expensive. Screen space reflections are cheaper, but have a problem with
things outside the screen, requiring another method to fall back on. IBL Cubemap reflections are a
good idea, as it synergizes well with the current clustering system, but has some PBR issues.

Cubemap reflections, hereforth called _Reflection Probes_, synergizes well with our clustering approach.
Each cluster could track both lights and reflection probes, meaning that local reflections could be possible.
One problem with reflection probes is that fragments not on the probe origin will be reflected incorrectly.
This can be compensated for using parallax correction, but this doesn't fully solve it.



#bibliography(style: "ieee", "bib.yml")
