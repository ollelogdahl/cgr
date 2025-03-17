#pragma once

#include "rend2/buffer.h"
#include "rend2/command_buffer.h"


struct AccelerationBLAS {
    VkAccelerationStructureKHR acc;
    GpuBuffer buffer;
};

class AccelerationBuilder {
public:
    AccelerationBuilder(gpu_t &gpu);

    AccelerationBLAS build_blas(
        CommandBuffer &cmd,
        const GpuBuffer &vertex_buffer,
        const GpuBuffer &index_buffer,
        u32 vertex_offset,
        u32 index_offset,
        u32 vertex_count,
        u32 index_count,
        u64 vertex_stride);

    void await_build(CommandBuffer &cmd);
private:
    gpu_t &m_gpu;

    std::vector<VkMemoryBarrier2> m_barriers;
};
