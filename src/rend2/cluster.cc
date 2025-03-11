#include "cluster.h"
#include "metrics.h"
#include "rend2/descriptor_set_layout_builder.h"
#include "rend2/pipeline_layout_builder.h"
#include "rend2/shader_compiler.h"
#include "rend2/vku.h"
#include <vulkan/vulkan_core.h>

struct alignas(16) ClusterGenPushConstants {
    m4f inv_proj;
    f32 znear;
    f32 zfar;
    u32 grid_x;
    u32 grid_y;
    u32 grid_z;
};

ClusterShading::ClusterShading(gpu_t &gpu, ShaderCompiler &sc, u32 x, u32 y, u32 z)
: m_gpu(gpu), m_x(x), m_y(y), m_z(z),
    m_cluster_buffer(gpu, x * y * z * sizeof(ClusterData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {

    metrics::gauge_u32("rend2.clusters.count", x * y * z);

    Shader cluster_gen = Shader({
        sc.compile("shaders/cluster-gen.comp")
    });

    VkDescriptorPool descriptor_pool;
    {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 },
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = array_size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = 1;

        VK_CHECK(vkCreateDescriptorPool(gpu.device, &pool_info, nullptr, &descriptor_pool));
    }

    VkDescriptorSetLayout ds_layout = DescriptorSetLayoutBuilder()
        .add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .build(gpu);

    m_descriptor_set.init(gpu, descriptor_pool, ds_layout);
    m_descriptor_set.write_storage_buffer(0, 0, m_cluster_buffer.get(), 0, VK_WHOLE_SIZE);
    m_descriptor_set.flush(gpu);

    m_gen_pipeline_layout = PipelineLayoutBuilder()
        .add_descriptor_set(ds_layout)
        .add_push_constant_range({VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ClusterGenPushConstants)})
        .build(gpu);

    m_gen_pipeline = create_compute_pipeline(gpu, m_gen_pipeline_layout, cluster_gen);
}

void ClusterShading::rebuild_clusters(CommandBuffer &cmd, f32 znear, f32 zfar, const m4f &m_inv_proj) {
    ClusterGenPushConstants push_constants = {
        .inv_proj = m_inv_proj,
        .znear = znear,
        .zfar = zfar,
        .grid_x = m_x,
        .grid_y = m_y,
        .grid_z = m_z,
    };

    VkDescriptorSet sets[] = { m_descriptor_set.get() };

    vkCmdBindPipeline(cmd.get(), VK_PIPELINE_BIND_POINT_COMPUTE, m_gen_pipeline);
    vkCmdBindDescriptorSets(cmd.get(), VK_PIPELINE_BIND_POINT_COMPUTE, m_gen_pipeline_layout, 0, 1, sets, 0, nullptr);
    vkCmdPushConstants(cmd.get(), m_gen_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ClusterGenPushConstants), &push_constants);
    vkCmdDispatch(cmd.get(), m_x, m_y, m_z);
}
