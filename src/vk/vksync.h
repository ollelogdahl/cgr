#pragma once

#include <span>

#include <vulkan/vulkan.h>

// this API is inspired by
// https://github.com/Tobski/simple_vulkan_synchronization

namespace vksync {

enum class AccessType {
    None = 0,
    IndirectBuffer,
    IndexBuffer,
    VertexBuffer,

    VertexShaderReadSampledImageOrUniformTexelBuffer,
    VertexShaderReadOther,

    FragmentShaderReadSampledImageOrUniformTexelBuffer,
    FragmentShaderReadColorInputAttachment,
    FragmentShaderReadDepthStencilInputAttachment,
    FragmentShaderReadOther,

    ColorAttachmentRead,
    DepthStencilAttachmentRead,

    ComputeShaderReadUniformBuffer,
    ComputeShaderReadSampledImageOrUniformTexelBuffer,
    ComputeShaderReadOther,

    AnyShaderReadOther,

    TransferRead,
    HostRead,

    Present,

    RayTracingShaderReadAccelerationStructure,
    AccelerationStructureBuildRead,

    _EndOfRead,

    VertexShaderWrite,
    FragmentShaderWrite,
    ColorAttachmentWrite,
    DepthStencilAttachmentWrite,

    ComputeShaderWrite,
    AnyShaderWrite,
    TransferWrite,
    HostPreinitialized,
    HostWrite,

    AccelerationStructureBuildWrite,

    General,

    _Count
};

// Define a set of accesses on multiple resources at once.
// This should be prefered if a buffer or image doesn't require queue ownership
// transfers, or if the image doesn't require a layout transition.
struct GlobalBarrier {
    std::span<const AccessType> prev;
    std::span<const AccessType> next;
};

void pipeline_barrier(VkCommandBuffer cmd, const std::span<const GlobalBarrier> &barriers);

}
