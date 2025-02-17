#include "planner.h"
#include "renderer.h"
#include <algorithm>

#include <numeric>
#include <tracy/Tracy.hpp>

std::vector<render_op_t> &RenderPlanner::plan_rendering(renderer_t &renderer) {
    ZoneScopedN("plan-rendering");
    // given the commands in commands, we need to generate rendering ops.
    //
    // @todo: I would like this function to take only the commands_t.
    // The issue is that draw_command -> draw_op needs to allocate an index
    // in the texture descriptor array, which is owned by the renderer. Oh well.
    // Same as for the const-ness. I don't love it.

    // we only care about the draw commands for now. In the future, lights will
    // also be important.

    ops.clear();
    // this ensures no dynamic allocations during the loop.
    ops.reserve(renderer.commands.draw_indexed.size() * 3);

    std::vector<u32> indices(renderer.commands.draw_indexed.size());
    std::iota(indices.begin(), indices.end(), 0);

    // sort the draw commands by material, then by vertex buffer.
    // @note: optimization: sort indices instead of the actual commands. 54% -> 1%
    std::sort(indices.begin(), indices.end(), [&](u32 a, u32 b) {
        auto &commands = renderer.commands.draw_indexed;
        auto &cmd_a = commands[a];
        auto &cmd_b = commands[b];

        // Compare materials first
        auto mat_a = cmd_a.material;
        auto mat_b = cmd_b.material;
        if (mat_a != mat_b) {
            return mat_a < mat_b;
        }

        // If materials are equal, compare index buffers
        return cmd_a.index_buffer < cmd_b.index_buffer;
    });

    // iterate over the sorted list and generate the ops.
    material_t *current_material = nullptr;
    gpu_buffer_t *current_vertex_buffer = nullptr;
    gpu_buffer_t *current_index_buffer = nullptr;
    for (auto idx : indices) {
        auto &cmd = renderer.commands.draw_indexed[idx];
        if (cmd.material != current_material) {
            ops.push_back(switch_material_op_t{
                .material = cmd.material,
                .albedo0_idx = renderer.get_or_create_texture_handle(cmd.material->tex_albedo0),
                .albedo1_idx = renderer.get_or_create_texture_handle(cmd.material->tex_albedo1),
                .albedo2_idx = renderer.get_or_create_texture_handle(cmd.material->tex_albedo2),
                .normal_idx = renderer.get_or_create_texture_handle(cmd.material->tex_normal),
                .roughness_idx = renderer.get_or_create_texture_handle(cmd.material->tex_roughness),
            });
            current_material = cmd.material;
        }

        if (cmd.vertex_buffer != current_vertex_buffer || cmd.index_buffer != current_index_buffer) {
            ops.push_back(switch_buffers_op_t{
                .vertex_buffer = cmd.vertex_buffer,
                .index_buffer = cmd.index_buffer,
            });
            current_vertex_buffer = cmd.vertex_buffer;
            current_index_buffer = cmd.index_buffer;
        }

        ops.push_back(draw_indexed_op_t{
            .index_count = cmd.index_count,
            .vertex_offset = 0,
            .index_offset = 0,
            .transform = cmd.transform,
        });
    }

    return ops;
}
