#pragma once

// parmesan: a mesh parameterization and chart generation library.
//
// used to generate texture coordinates with minimal geodesic distortion (stretch).

// simple usage:
//
// unsigned int num_vertices = ...;
// unsigned int num_triangles = ...;
// vector<float> positions = { ... };
// vector<u32> indices = { ... };
//
// vector<float> uvs(num_vertices * 2);
// vector<u32> chart_indices(num_vertices);
// parmesan_atlas(
//      positions.data, num_vertices, 3 * sizeof(float),
//      indices.data(), num_triangles,
//      0.1f,
//      uvs.data(), 2 * sizeof(float),
//      chart_indices.data(), sizeof(u32)
// );
//
// The attributes can also be interlaced quite easily.
//
// struct vertex_t {
//    float position[3];
//    ...
//    float uv[2];
//    u32 chart_index;
// };
//
// unsigned int num_vertices = ...;
// unsigned int num_triangles = ...;
// vector<u32> indices = { ... };

// currently, this is just a simpler interface into uvatlas by microsoft.
// positions:           input vertex positions
// indices:             input triangle indices
// tringle_count:       number of triangles
// max_stretch:         maximum stretch allowed in the parameterization
// out_uvs:             output uv coordinates
// out_charts:          output chart indices
int parmesan_atlas(
    float *positions, unsigned int positions_count, unsigned int position_stride,
    unsigned int *indices, unsigned int tringle_count,
    float max_stetch,
    float *out_uvs, unsigned int uv_stride,
    unsigned int *out_charts, unsigned int chart_stride
);
