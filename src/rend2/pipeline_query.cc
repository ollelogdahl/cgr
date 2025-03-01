#include "pipeline_query.h"
#include "rend2/command_buffer.h"

PipelineQuery::PipelineQuery(gpu_t &gpu) : m_gpu(gpu) {
    VkQueryPoolCreateInfo query_pool_info = {};
	query_pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	query_pool_info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
	query_pool_info.pipelineStatistics =
		VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
		VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
		VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
		VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
		VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
		VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
	query_pool_info.queryCount = 1;

	VK_CHECK(vkCreateQueryPool(gpu.device, &query_pool_info, nullptr, &m_query_pool));
	m_last_results.resize(6);
	m_results_and_avail.resize(6 + 1);
}

void PipelineQuery::begin(CommandBuffer &cmd) {
    if (m_in_flight) {
        u32 size = m_results_and_avail.size() * sizeof(u64);
        vkGetQueryPoolResults(m_gpu.device, m_query_pool, 0, 1, size,
            m_results_and_avail.data(), size,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT
        );

        bool available = m_results_and_avail.back() != 0;
        if (available) {
            m_in_flight = false;

            m_last_results = std::vector<u64>(
                m_results_and_avail.begin(),
                m_results_and_avail.end() - 1
            );
        }
    }

    if (!m_in_flight) {
        m_in_flight = true;
        vkCmdResetQueryPool(cmd.get(), m_query_pool, 0, 1);
        vkCmdBeginQuery(cmd.get(), m_query_pool, 0, 0);
        m_started = true;
    }
}
void PipelineQuery::end(CommandBuffer &cmd) {
    if (m_started) {
        vkCmdEndQuery(cmd.get(), m_query_pool, 0);
        m_started = false;
    }
}

std::span<const u64> PipelineQuery::get_results() const {
    return std::span<const u64>(m_last_results);
}
