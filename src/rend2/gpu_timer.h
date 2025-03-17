#pragma once


#include "gpu.h"
#include "oc.h"

class GpuTimer {
private:
    static constexpr u32 QUERIES_PER_FRAME = 2; // Start and end query for each frame

    gpu_t& gpu;
    std::array<VkQueryPool, MAX_FRAMES_IN_FLIGHT> query_pools;
    float timestamp_period;
    uint32_t current_frame = 0;

public:
    GpuTimer(gpu_t& gpu) : gpu(gpu) {
        timestamp_period = gpu.limits.timestamp_period * 1e-6f; // Convert to ms

        // Create query pools for each frame in flight
        VkQueryPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        pool_info.queryCount = QUERIES_PER_FRAME;

        for (auto& pool : query_pools) {
            VK_CHECK(vkCreateQueryPool(gpu.device, &pool_info, nullptr, &pool));
        }
    }

    ~GpuTimer() {
        for (auto& pool : query_pools) {
            vkDestroyQueryPool(gpu.device, pool, nullptr);
        }
    }

    void begin_frame(VkCommandBuffer cmd, uint32_t frame_index) {
        current_frame = frame_index % MAX_FRAMES_IN_FLIGHT;
        vkCmdResetQueryPool(cmd, query_pools[current_frame], 0, QUERIES_PER_FRAME);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           query_pools[current_frame], 0);
    }

    void end_frame(VkCommandBuffer cmd) {
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                           query_pools[current_frame], 1);
    }

    float get_gpu_time() {
        uint64_t timestamps[2];
        VK_CHECK(vkGetQueryPoolResults(gpu.device,
            query_pools[current_frame],
            0, 2,
            sizeof(timestamps),
            timestamps,
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

        return float(timestamps[1] - timestamps[0]) * timestamp_period;
    }
};
