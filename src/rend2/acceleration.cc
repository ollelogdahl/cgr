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
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

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
    auto &scratch_buffer = m_scratch_buffers.emplace_back(m_gpu, size_info.buildScratchSize,
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

    VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
	accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	accelerationDeviceAddressInfo.accelerationStructure = acceleration_structure;
	auto addr = vkGetAccelerationStructureDeviceAddressKHR(m_gpu.device, &accelerationDeviceAddressInfo);

    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    m_barriers.push_back(barrier);

    return { acceleration_structure, std::move(as_buffer), addr };
}

void AccelerationBuilder::await_build(CommandBuffer &cmd) {
    TracyVkZone(cmd.tracy_ctx(), cmd.get(), "await-accel-build");

    VkDependencyInfoKHR dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
    dependency.bufferMemoryBarrierCount = m_buffer_barriers.size();
    dependency.pBufferMemoryBarriers = m_buffer_barriers.data();
    dependency.memoryBarrierCount = m_barriers.size();
    dependency.pMemoryBarriers = m_barriers.data();

    vkCmdPipelineBarrier2(cmd.get(), &dependency);

    m_buffer_barriers.clear();
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

AccelerationTLAS AccelerationBuilder::build_tlas(
    CommandBuffer &cmd,
    const std::vector<AccelerationInstance> &instances
) {
    TracyVkZone(cmd.tracy_ctx(), cmd.get(), "build-tlas");

    // Create a buffer to store the instances
    std::vector<VkAccelerationStructureInstanceKHR> instance_data(instances.size());
    for (size_t i = 0; i < instances.size(); ++i) {
        auto &instance = instances[i];
        auto &dst = instance_data[i];

        memcpy(dst.transform.matrix, instance.transform, 12 * sizeof(f32));
        dst.instanceCustomIndex = instance.custom_index;
        dst.mask = 0xFF;
        dst.instanceShaderBindingTableRecordOffset = 0;
        dst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        dst.accelerationStructureReference = instance.blas->device_address;
    }

    for (size_t i = 0; i < instance_data.size(); ++i) {
        fmt::println("tlas instance {}: blas-addr: {}", i, instance_data[i].accelerationStructureReference);
    }

    GpuBuffer instance_buffer(m_gpu,
        instance_data.size() * sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    auto instance_buffer_dep = instance_buffer.write_with_barrier(cmd.get(), slice<byte>(
        (byte*)instance_data.data(),
        instance_data.size() * sizeof(VkAccelerationStructureInstanceKHR)));

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = instance_buffer.get_buffer_device_address();

    u32 primitive_count = instances.size();

    // Figure out the size info
    VkAccelerationStructureBuildSizesInfoKHR size_info{};
    {
        VkAccelerationStructureBuildGeometryInfoKHR build_info{};
        build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build_info.geometryCount = 1;
        build_info.pGeometries = &geometry;

        size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

        vkGetAccelerationStructureBuildSizesKHR(
            m_gpu.device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &build_info,
            &primitive_count,
            &size_info
        );
    }
    fmt::println("TLAS size: {}", size_info.accelerationStructureSize);
    fmt::println("TLAS scratch size: {}", size_info.buildScratchSize);
    fmt::println("TLAS instance count: {}", instances.size());
    fmt::println("TLAS instance size: {}", sizeof(VkAccelerationStructureInstanceKHR));


    // Create the acceleration structure buffer
    GpuBuffer as_buffer(m_gpu, size_info.accelerationStructureSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);

    // Create the acceleration structure
    VkAccelerationStructureKHR acceleration_structure;
    {
        VkAccelerationStructureCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        create_info.buffer = as_buffer.get();
        create_info.size = size_info.accelerationStructureSize;
        create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

        vkCreateAccelerationStructureKHR(m_gpu.device, &create_info, nullptr, &acceleration_structure);
    }

    // Create scratch buffer
    auto &scratch_buffer = m_scratch_buffers.emplace_back(m_gpu, size_info.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    VkAccelerationStructureBuildGeometryInfoKHR build_info{};
    build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_info.dstAccelerationStructure = acceleration_structure;
    build_info.geometryCount = 1;
    build_info.pGeometries = &geometry;
    build_info.scratchData.deviceAddress = scratch_buffer.get_buffer_device_address();

    VkAccelerationStructureBuildRangeInfoKHR build_range_info{};
    build_range_info.primitiveCount = instances.size();
    build_range_info.primitiveOffset = 0;
    build_range_info.firstVertex = 0;
    build_range_info.transformOffset = 0;

    const VkAccelerationStructureBuildRangeInfoKHR* build_range_infos[] = { &build_range_info };

    fmt::println("building TLAS acceleration structure");

    {
        // wait for instance data write before building the TLAS
        instance_buffer_dep.pipeline_barrier(cmd.get(),
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    }
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
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    m_barriers.push_back(barrier);

    return { acceleration_structure, std::move(as_buffer), std::move(instance_buffer) };
}

AccelerationTLAS AccelerationBuilder::rebuild_tlas(
    CommandBuffer &cmd,
    AccelerationTLAS &tlas,
    const std::vector<AccelerationInstance> &instances
) {
    // For an update, we'll just build a new TLAS for simplicity
    // Can be optimized later using VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR

    // First, destroy the old TLAS
    vkDestroyAccelerationStructureKHR(m_gpu.device, tlas.acc, nullptr);

    // Then build a new one
    return build_tlas(cmd, instances);
}
