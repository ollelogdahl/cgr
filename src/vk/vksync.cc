#include "vksync.h"
#include <vulkan/vulkan_core.h>

namespace vksync {

struct AccessInfo {
    VkPipelineStageFlags stage;
    VkAccessFlags access;
    VkImageLayout layout;
};

static const AccessInfo access_map[] = {
    { // None
        .stage = VK_PIPELINE_STAGE_NONE,
        .access = VK_ACCESS_NONE,
        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
    },
    { // IndirectBuffer
        .stage = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        .access = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
    },
    { // IndexBuffer
        .stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        .access = VK_ACCESS_INDEX_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
    },
    { // VertexBuffer
        .stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        .access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
    },
    { // VertexShaderReadSampledImageOrUniformTexelBuffer
        .stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        .access = VK_ACCESS_SHADER_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    },
    { // VertexShaderReadOther
        .stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        .access = VK_ACCESS_SHADER_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    },
    { // FragmentShaderReadSampledImageOrUniformTexelBuffer
        .stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .access = VK_ACCESS_SHADER_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    },
    { // FragmentShaderReadColorInputAttachment
        .stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .access = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    },
    { // FragmentShaderReadDepthStencilInputAttachment
        .stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
    },
    { // FragmentShaderReadOther
        .stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .access = VK_ACCESS_SHADER_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    },
    { // ColorAttachmentRead
        .stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    },
    { // DepthStencilAttachmentRead
        .stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        .access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
    },
    { // ComputeShaderReadUniformBuffer
        .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .access = VK_ACCESS_UNIFORM_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    },
    { // ComputeShaderReadSampledImageOrUniformTexelBuffer
        .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .access = VK_ACCESS_SHADER_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    },
    { // ComputeShaderReadOther
        .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .access = VK_ACCESS_SHADER_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    },
    { // AnyShaderReadOther
        .stage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .access = VK_ACCESS_SHADER_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    },
    { // TransferRead
        .stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .access = VK_ACCESS_TRANSFER_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    },
    { // HostRead
        .stage = VK_PIPELINE_STAGE_HOST_BIT,
        .access = VK_ACCESS_HOST_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    },
    { // Present
        .stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        .access = VK_ACCESS_MEMORY_READ_BIT,
        .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    },
    { // RayTracingShaderReadAccelerationStructure
        .stage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        .access = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    },
    { // AccelerationStructureBuildRead
        .stage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        .access = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    },
    { // _EndOfRead

    },
    { // VertexShaderWrite
        .stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        .access = VK_ACCESS_SHADER_WRITE_BIT,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    },
    { // FragmentShaderWrite
        .stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .access = VK_ACCESS_SHADER_WRITE_BIT,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    },
    { // ColorAttachmentWrite
        .stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    },
    { // DepthStencilAttachmentWrite
        .stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        .access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    },
    { // ComputeShaderWrite
        .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .access = VK_ACCESS_SHADER_WRITE_BIT,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    },
    { // AnyShaderWrite
        .stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        .access = VK_ACCESS_SHADER_WRITE_BIT,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    },
    { // TransferWrite
        .stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .access = VK_ACCESS_TRANSFER_WRITE_BIT,
        .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    },
    { // HostPreinitialized
        .stage = VK_PIPELINE_STAGE_HOST_BIT,
        .access = VK_ACCESS_HOST_WRITE_BIT,
        .layout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    },
    { // HostWrite
        .stage = VK_PIPELINE_STAGE_HOST_BIT,
        .access = VK_ACCESS_HOST_WRITE_BIT,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    },
    { // AccelerationStructureBuildWrite
        .stage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        .access = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
    },
    { // General
        .stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        .access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    }
};

void pipeline_barrier(VkCommandBuffer cmd, const std::span<const GlobalBarrier> &barriers) {}

}
