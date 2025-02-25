#pragma once

#include "gpu.h"

void alloc_command_buffers(gpu_t &gpu, VkCommandPool pool, u32 count, VkCommandBuffer *buffers);

struct CommandBuffer {
public:
    CommandBuffer() = default;
    CommandBuffer(VkCommandBuffer cmd, TracyVkCtx ctx) {
        m_cmd = cmd;
        m_tracy_ctx = ctx;
    }
    CommandBuffer(gpu_t &gpu, VkQueue queue, VkCommandPool pool, const char *name);

    void reset_begin();
    void end();
    VkCommandBuffer get() { return m_cmd; }
    TracyVkCtx tracy_ctx() { return m_tracy_ctx; }
private:
    VkCommandBuffer m_cmd;
    TracyVkCtx m_tracy_ctx;
};
