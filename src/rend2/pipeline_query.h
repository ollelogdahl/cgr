#pragma once

#include "gpu.h"
#include "rend2/command_buffer.h"
#include <span>

class PipelineQuery {
public:
    // @todo: allow specifying the query pool.
    // @todo: allow selecting which queries to enable.
    PipelineQuery(gpu_t &gpu);

    void begin(CommandBuffer &cmd);
    void end(CommandBuffer &cmd);

    std::span<const u64> get_results() const;
private:
    gpu_t &m_gpu;
    bool m_in_flight = false;
    bool m_started = false;

    VkQueryPool m_query_pool;
    std::vector<u64> m_last_results;
    std::vector<u64> m_results_and_avail;
};
