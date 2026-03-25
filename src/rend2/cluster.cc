#include "cluster.h"
#include "metrics.h"
#include "rend2/descriptor_set_layout_builder.h"
#include "rend2/pipeline_layout_builder.h"
#include "rend2/shader_compiler.h"
#include "rend2/vku.h"


struct alignas(16) ClusterGenPushConstants {
    m4f inv_proj;
    f32 znear;
    f32 zfar;
    u32 grid_x;
    u32 grid_y;
    u32 grid_z;
};

struct alignas(16) ClusterAssignPushConstants {
    m4f view;
    v4f frustum_planes[6];
    f32 light_threshold;
};

struct alignas(16) ClusterConstants {
    f32 znear;
    f32 zfar;
    u32 grid_x;
    u32 grid_y;
    u32 grid_z;
};

const f32 max_cluster_items_factor = 0.5;

u32 cluster_buffer_size(const ClusterConfig &config) {
    return sizeof(ClusterConstants) + config.grid_x * config.grid_y * config.grid_z * sizeof(ClusterShading::ClusterData);
}
u32 items_buffer_size(const ClusterConfig &config) {
    return (config.grid_x * config.grid_y * config.grid_z)
        * ClusterShading::MAX_LIGHTS_PER_CLUSTER * max_cluster_items_factor * sizeof(u32) + sizeof(u32);
}

ClusterShading::ClusterShading(gpu_t &gpu, ShaderCompiler &sc, const ClusterConfig &config)
: m_gpu(gpu), m_config(config),
    m_cluster_buffer(gpu, cluster_buffer_size(config), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    m_items_buffer(gpu, items_buffer_size(config), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferType::Readback) {

    set_object_name(gpu, VK_OBJECT_TYPE_BUFFER, m_cluster_buffer.get(), "cluster-buffer");
    set_object_name(gpu, VK_OBJECT_TYPE_BUFFER, m_items_buffer.get(), "cluster-items-buffer");

    metrics::gauge_u32("rend2.clusters.count", num_clusters());

    VkDescriptorPool descriptor_pool;
    {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 },
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = array_size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = 2;

        VK_CHECK(vkCreateDescriptorPool(gpu.device, &pool_info, nullptr, &descriptor_pool));
    }

    VkDescriptorSetLayout ds_gen_layout = DescriptorSetLayoutBuilder()
        .add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .build(gpu);

    VkDescriptorSetLayout ds_assign_layout = DescriptorSetLayoutBuilder()
        .add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .add_binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
        .build(gpu);

    m_gen_descriptor_set.init(gpu, descriptor_pool, ds_gen_layout);
    m_gen_descriptor_set.write_storage_buffer(0, 0, m_cluster_buffer.get(), 0, VK_WHOLE_SIZE);
    m_gen_descriptor_set.flush(gpu);

    m_gen_pipeline_layout = PipelineLayoutBuilder()
        .add_descriptor_set(ds_gen_layout)
        .add_push_constant_range({VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ClusterGenPushConstants)})
        .build(gpu);

    m_assign_descriptor_set.init(gpu, descriptor_pool, ds_assign_layout);
    m_assign_descriptor_set.write_storage_buffer(0, 0, m_cluster_buffer.get(), 0, VK_WHOLE_SIZE);
    m_assign_descriptor_set.write_storage_buffer(1, 0, m_items_buffer.get(), 0, VK_WHOLE_SIZE);
    m_assign_descriptor_set.write_storage_buffer(2, 0, m_config.light_buffer.get(), 0, VK_WHOLE_SIZE);
    m_assign_descriptor_set.flush(gpu);

    m_assign_pipeline_layout = PipelineLayoutBuilder()
        .add_descriptor_set(ds_assign_layout)
        .add_push_constant_range({VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ClusterAssignPushConstants)})
        .build(gpu);

    Shader cluster_gen = Shader({
        sc.compile("shaders/cluster-gen.comp")
    });

    Shader cluster_assign = Shader({
        sc.compile("shaders/cluster-assign.comp")
    });

    m_gen_pipeline = create_compute_pipeline(gpu, m_gen_pipeline_layout, cluster_gen);
    m_assign_pipeline = create_compute_pipeline(gpu, m_assign_pipeline_layout, cluster_assign);

    set_object_name(gpu, VK_OBJECT_TYPE_PIPELINE, m_gen_pipeline, "cluster-gen");
    set_object_name(gpu, VK_OBJECT_TYPE_PIPELINE, m_assign_pipeline, "cluster-assign");
}

void ClusterShading::rebuild_clusters(CommandBuffer &cmd, f32 znear, f32 zfar, const m4f &m_inv_proj) {
    TracyVkZone(cmd.tracy_ctx(), cmd.get(), "rebuild-clusters");

    ClusterGenPushConstants push_constants = {
        .inv_proj = m_inv_proj,
        .znear = znear,
        .zfar = zfar,
        .grid_x = m_config.grid_x,
        .grid_y = m_config.grid_y,
        .grid_z = m_config.grid_z,
    };

    VkDescriptorSet sets[] = { m_gen_descriptor_set.get() };


    vkCmdBindPipeline(cmd.get(), VK_PIPELINE_BIND_POINT_COMPUTE, m_gen_pipeline);
    vkCmdBindDescriptorSets(cmd.get(), VK_PIPELINE_BIND_POINT_COMPUTE, m_gen_pipeline_layout, 0, 1, sets, 0, nullptr);
    vkCmdPushConstants(cmd.get(), m_gen_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ClusterGenPushConstants), &push_constants);

    u32 group_count = (num_clusters() + 127) / 128;
    vkCmdDispatch(cmd.get(), group_count, 1, 1);
}

void ClusterShading::assign_items(CommandBuffer &cmd, const m4f &view_proj, const m4f &view_matrix) {
    TracyVkZone(cmd.tracy_ctx(), cmd.get(), "assign-items");

    ClusterAssignPushConstants push_constants = {
        .view = view_matrix,
        .frustum_planes = {},
        .light_threshold = runtime_config.light_threshold,
    };
    m4f::extract_planes(view_proj, push_constants.frustum_planes);

    auto full_barrier = [&]() {
        VkMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd.get(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    };

    // full barrier
    full_barrier();

    // read the number of items last frame
    // {
    //     u32 *num_items = m_items_buffer.read<u32>(0);
    //     metrics::gauge_u32("rend2.clusters.items", *num_items);
    // }

    // full_barrier();

    // clear the count field in the items buffer.
    vkCmdFillBuffer(cmd.get(), m_items_buffer.get(), 0, sizeof(u32), 0);

    full_barrier();

    VkDescriptorSet sets[] = { m_assign_descriptor_set.get() };

    vkCmdBindPipeline(cmd.get(), VK_PIPELINE_BIND_POINT_COMPUTE, m_assign_pipeline);
    vkCmdBindDescriptorSets(cmd.get(), VK_PIPELINE_BIND_POINT_COMPUTE, m_assign_pipeline_layout, 0, 1, sets, 0, nullptr);
    vkCmdPushConstants(cmd.get(), m_assign_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ClusterAssignPushConstants), &push_constants);

    u32 group_count = (num_clusters() + 127) / 128;
    vkCmdDispatch(cmd.get(), group_count, 1, 1);
}
