#include "acceleration.h"

#include <tracy/TracyVulkan.hpp>

AccelerationBuilder::AccelerationBuilder(gpu_t &gpu) : m_gpu(gpu) {}

AccelerationBLAS AccelerationBuilder::build_blas(
    CommandBuffer &cmd,
    const GpuBuffer &vertex_buffer,
    const GpuBuffer &index_buffer,
    u32 vertex_offset,
    u32 index_offset,
    u32 vertex_count,
    u32 index_count,
    u64 vertex_stride
) {
    TracyVkZone(cmd.tracy_ctx(), cmd.get(), "build-blas");

    // figure out te size info
    VkAccelerationStructureBuildSizesInfoKHR size_info{};
    {
        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geometry.geometry.triangles.maxVertex = vertex_count;
        geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;

        VkAccelerationStructureBuildGeometryInfoKHR build_info{};
        build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build_info.geometryCount = 1;
        build_info.pGeometries = &geometry;

        size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

        vkGetAccelerationStructureBuildSizesKHR(
            m_gpu.device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &build_info,
            &index_count,
            &size_info
        );
    }

    // Create the acceleration structure buffer
    GpuBuffer as_buffer(m_gpu, size_info.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);

    // Create the acceleration structure
    VkAccelerationStructureCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    create_info.buffer = as_buffer.get();
    create_info.size = size_info.accelerationStructureSize;
    create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    VkAccelerationStructureKHR acceleration_structure;
    vkCreateAccelerationStructureKHR(m_gpu.device, &create_info, nullptr, &acceleration_structure);

    // Build the acceleration structure
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geometry.geometry.triangles.vertexData.deviceAddress =
        vertex_buffer.get_buffer_device_address() + vertex_offset * vertex_stride;
    geometry.geometry.triangles.vertexStride = vertex_stride;
    geometry.geometry.triangles.maxVertex = vertex_count;
    geometry.geometry.triangles.indexData.deviceAddress =
        index_buffer.get_buffer_device_address() + index_offset * sizeof(uint32_t);
    geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;

    VkAccelerationStructureBuildGeometryInfoKHR build_info{};
    build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_info.dstAccelerationStructure = acceleration_structure;
    build_info.geometryCount = 1;
    build_info.pGeometries = &geometry;

    // Create scratch buffer
    GpuBuffer scratch_buffer(m_gpu, size_info.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    build_info.scratchData.deviceAddress = scratch_buffer.get_buffer_device_address();

    VkAccelerationStructureBuildRangeInfoKHR build_range_info{};
    build_range_info.primitiveCount = index_count / 3;
    build_range_info.primitiveOffset = 0;
    build_range_info.firstVertex = 0;
    build_range_info.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* build_range_infos[] = { &build_range_info };

    fmt::println("building acceleration structure");

    vkCmdBuildAccelerationStructuresKHR(
        cmd.get(),
        1,
        &build_info,
        build_range_infos
    );

    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    m_barriers.push_back(barrier);

    return { acceleration_structure, std::move(as_buffer) };
}

void AccelerationBuilder::await_build(CommandBuffer &cmd) {
    TracyVkZone(cmd.tracy_ctx(), cmd.get(), "await-accel-build");

    VkDependencyInfoKHR dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
    dependency.memoryBarrierCount = m_barriers.size();
    dependency.pMemoryBarriers = m_barriers.data();

    vkCmdPipelineBarrier2(cmd.get(), &dependency);

    m_barriers.clear();

    // @todo: i think we are free now also to destroy scratch buffers.
    // figure out how to do this.
    //
    // Options:
    //  1. Create a new scratch buffer for every build.
    //  2. Recycle single buffer.
    //  3. Keep a list of scratch buffers (balance 1/2)
    //  4. New, but recycle if too much memory.
}
