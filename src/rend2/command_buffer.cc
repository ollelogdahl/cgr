#include "command_buffer.h"

CommandBuffer::CommandBuffer(gpu_t &gpu, VkQueue queue, VkCommandPool pool, const char *name) {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VK_CHECK(vkAllocateCommandBuffers(gpu.device, &alloc_info, &m_cmd));
    m_tracy_ctx = TracyVkContext(gpu.pdev, gpu.device, queue, m_cmd);
    TracyVkContextName(m_tracy_ctx, name, strlen(name));
}

void CommandBuffer::reset_begin() {
    VK_CHECK(vkResetCommandBuffer(m_cmd, 0));

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(m_cmd, &begin_info));
}

void CommandBuffer::end() {
    VK_CHECK(vkEndCommandBuffer(m_cmd));
}

void alloc_command_buffers(gpu_t &gpu, VkCommandPool pool, u32 count, VkCommandBuffer *buffers) {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = count;

    VK_CHECK(vkAllocateCommandBuffers(gpu.device, &alloc_info, buffers));
}
