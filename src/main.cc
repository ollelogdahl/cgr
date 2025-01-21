#include <asm-generic/errno-base.h>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <stdexcept>

#include <sys/types.h>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "oc.h"
#include "log.h"

#include <iostream>
#include <vector>
#include <cstring>
#include <set>
#include <vulkan/vulkan_core.h>

static logger_t gpu_log = logger_t("gpu");

#include <sys/inotify.h>
#include <fcntl.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_vulkan.h>
#include <implot/implot.h>

#include "gpu.h"
#include "modimp.h"

struct fswatcher_t {
    int inotify_fd;

    struct elem_t {
        void (*on_modified)(std::string, void *userdata);
        void *userdata;
        std::string path;

        bool operator==(const elem_t &other) const {
            return path == other.path && on_modified == other.on_modified;
        }
    };

    std::unordered_map<int, elem_t> watches;

    void init() {
        inotify_fd = inotify_init();
        auto old_flags = fcntl(inotify_fd, F_GETFL);
        fcntl(inotify_fd, F_SETFL, old_flags | O_NONBLOCK);
    }

    void add_watch(const char *path, void (*on_modified)(std::string, void *userdata), void *userdata) {
        int ret = inotify_add_watch(inotify_fd, path, IN_ALL_EVENTS);
        if (ret == -1) {
            panic("inotify_add_watch error: {}", strerror(errno));
        }
        watches[ret] = {
            on_modified,
            userdata,
            path
        };
    }
    void process_watches() {
        // read from inotify until no more events are available now
        const usize event_max_size = sizeof(inotify_event) + 256;
        const usize buffer_size = 128 * event_max_size;
        char buffer[buffer_size];

        while (true) {
            auto len = read(inotify_fd, buffer, buffer_size);
            if (len < 0 && errno == EAGAIN) break;
            if (len == 0) break;

            ssize_t idx = 0;
            while(idx < len) {
                auto ev = (inotify_event *)(buffer + idx);
                idx += sizeof(inotify_event) + ev->len;

                if (ev->mask & IN_MODIFY) {
                    auto &e = watches[ev->wd];
                    e.on_modified(e.path, e.userdata);
                }
            }
        }
    }
};

#include <sys/stat.h>

bool file_a_is_newer_than_b(const char *a, const char *b) {
    struct stat a_stat;
    struct stat b_stat;

    int ret_a = stat(a, &a_stat);
    int ret_b = stat(b, &b_stat);

    if (ret_b != 0) {
        return true;
    }
    if (ret_a != 0) {
        return false;
    }

    return a_stat.st_mtime > b_stat.st_mtime;
}

struct shader_program_load_params_t {
    const char *vertex_hlsl_path;
    const char *fragment_hlsl_path;

    bool operator==(const shader_program_load_params_t &other) const {
        return strcmp(vertex_hlsl_path, other.vertex_hlsl_path) == 0 &&
               strcmp(fragment_hlsl_path, other.fragment_hlsl_path) == 0;
    }
};
struct shader_program_t {
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    shader_program_load_params_t params;
    bool modified = false;
};

// @todo: setup a pipeline cache which will allow us to
// 1. create only 1 pipeline for each configuration
// 2. Hot reload the pipeline when the shader changes
struct pipeline_config_t {
    ref_t<shader_program_t> shader;

    // @todo: make this cleaner and make optional
    struct {
        slice<const VkVertexInputBindingDescription> bindings;
        slice<const VkVertexInputAttributeDescription> attributes;
    } vertex_input_info;
    VkPipelineInputAssemblyStateCreateInfo input_assembly;
    VkPipelineViewportStateCreateInfo viewport_state;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineMultisampleStateCreateInfo multisampling;
    slice<const VkDynamicState> dynamic_state;
    VkPipelineLayout pipeline_layout;
    VkRenderPass render_pass;

    bool operator ==(const pipeline_config_t &other) const {
        return shader == other.shader;
    }
};
struct gpu_pipeline_t {
    VkPipeline pipeline;
    pipeline_config_t config;

    std::vector<VkDynamicState> dynamic_states;
    struct {
        std::vector<VkVertexInputBindingDescription> bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;
    } vertex_input_info;

    bool modified = false;
};

template <>
struct std::hash<shader_program_load_params_t> {
    std::size_t operator()(const shader_program_load_params_t &params) const {
        std::size_t h1 = std::hash<const char*>{}(params.vertex_hlsl_path);
        std::size_t h2 = std::hash<const char*>{}(params.fragment_hlsl_path);
        return h1 ^ (h2 << 1);
    }
};

template <>
struct std::hash<pipeline_config_t> {
    std::size_t operator()(const pipeline_config_t &config) const {
        std::size_t h1 = std::hash<shader_program_load_params_t>{}(config.shader->params);
        return h1;
    }
};

#define SHADER_STAGE_VERTEX 0
#define SHADER_STAGE_FRAGMENT 1

VkPipelineShaderStageCreateInfo compile_shader(gpu_t &gpu, const char *path, int type) {
    // in dev mode, we compile the shader into the tmp dir. The filename in tmp is
    // based on the hash of the original file name.

    u64 hash = std::hash<const char *>{}(path);

    const char *tmp_dir = "/tmp";
    auto tmp_path = fmt::format("{}/{}.spv", tmp_dir, hash);

    VkShaderStageFlagBits vk_stage;
    const char *glslc_stage;
    switch (type) {
    case SHADER_STAGE_VERTEX:
        glslc_stage = "vertex";
        vk_stage = VK_SHADER_STAGE_VERTEX_BIT;
        break;
    case SHADER_STAGE_FRAGMENT:
        glslc_stage = "fragment";
        vk_stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        break;
    default:
        panic("unknown shader stage");
    }

    if (file_a_is_newer_than_b(path, tmp_path.c_str())) {
        gpu_log.info("compiling shader {}", path);
        // @todo: the path to glslc should maybe be compile-time configurable? or taken from env?
        auto cmd = fmt::format("/home/dv20/dv20oll/bin/glslc -fshader-stage={} -o {} {}", glslc_stage, tmp_path, path);

        // @todo: use exec instead of system.
        // we want to be able to do these things in parallel i think.
        // for this, we will break this function into two, (try_invoke_compiler, create_shader),
        // and then we can call try_invoke_compiler in parallel.
        system(cmd.c_str());
    }

    auto code = file_read(tmp_path.c_str()).unwrap();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.contents.len;
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.contents.data);

    VkShaderModule shader_module;
    if (vkCreateShaderModule(gpu.device, &createInfo, nullptr, &shader_module) != VK_SUCCESS) {
        gpu_log.error("failed to create shader module");
    }

    file_close(code);

    VkPipelineShaderStageCreateInfo stage_info{};
    stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_info.stage = vk_stage;
    stage_info.module = shader_module;
    stage_info.pName = "main";

    return stage_info;
}

// @todo: pipelines should be somewhere else (pipeline cache), but needs to be referenced from here.
struct loader_t {
    ref_t<shader_program_t> load_shader_program(const shader_program_load_params_t &params) {
        auto exists_it = loaded_shaders.find(params);
        if (exists_it != loaded_shaders.end()) {
            return exists_it->second;
        }

        std::vector<VkPipelineShaderStageCreateInfo> stages;
        stages.push_back(compile_shader(*gpu, params.vertex_hlsl_path, SHADER_STAGE_VERTEX));
        stages.push_back(compile_shader(*gpu, params.fragment_hlsl_path, SHADER_STAGE_FRAGMENT));

        loaded_shaders[params] = make_ref<shader_program_t>();
        shader_program_t &program = *loaded_shaders[params];
        program.stages = stages;
        program.params = params;
        program.modified = false;

        watcher.add_watch(params.vertex_hlsl_path, [](std::string, void *userdata) {
            auto *shader = static_cast<shader_program_t *>(userdata);
            shader->modified = true;
        }, &program);
        watcher.add_watch(params.fragment_hlsl_path, [](std::string, void *userdata) {
            auto *shader = static_cast<shader_program_t *>(userdata);
            shader->modified = true;
        }, &program);

        return loaded_shaders[params];
    }

    // @todo: I am not a fan of the fact that pipeline creation is a part of the resource loader.
    // In my opinion, it should be a part of the gpu. The issue is that hotreloading requires
    // reconstructing pipelines. It should be easy to move it.
    ref_t<gpu_pipeline_t> make_pipeline(const pipeline_config_t &config) {
        auto exists_it = loaded_pipelines.find(config);
        if (exists_it != loaded_pipelines.end()) {
            return exists_it->second;
        }

        auto pipeline = make_ref<gpu_pipeline_t>();
        pipeline->pipeline = VK_NULL_HANDLE;
        pipeline->config = config;
        pipeline->modified = true;

        // @todo: the config can contain temporary pointers (like slices to descriptors).
        // these need to be copied into the pipeline struct.
        pipeline->vertex_input_info.bindings = std::vector<VkVertexInputBindingDescription>(config.vertex_input_info.bindings.len);
        pipeline->vertex_input_info.attributes = std::vector<VkVertexInputAttributeDescription>(config.vertex_input_info.attributes.len);
        memcpy(pipeline->vertex_input_info.bindings.data(), config.vertex_input_info.bindings.data, config.vertex_input_info.bindings.len * sizeof(VkVertexInputBindingDescription));
        memcpy(pipeline->vertex_input_info.attributes.data(), config.vertex_input_info.attributes.data, config.vertex_input_info.attributes.len * sizeof(VkVertexInputAttributeDescription));

        pipeline->dynamic_states = std::vector<VkDynamicState>(config.dynamic_state.len);
        memcpy(pipeline->dynamic_states.data(), config.dynamic_state.data, config.dynamic_state.len * sizeof(VkDynamicState));

        loaded_pipelines[config] = pipeline;

        return pipeline;
    }

    void init(gpu_t &gpu) {
        this->gpu = &gpu;
        watcher.init();
    }

    void process_hotreload() {
        watcher.process_watches();

        for (auto it : loaded_pipelines) {
            auto &pipeline = it.second;
            if (pipeline->config.shader->modified) {
                pipeline->modified = true;
            }
        }

        for (auto it : loaded_shaders) {
            auto &program = it.second;
            if (program->modified) {
                program->stages.clear();

                program->stages.push_back(compile_shader(*gpu, program->params.vertex_hlsl_path, SHADER_STAGE_VERTEX));
                program->stages.push_back(compile_shader(*gpu, program->params.fragment_hlsl_path, SHADER_STAGE_FRAGMENT));

                program->modified = false;
            }
        }

        for (auto it : loaded_pipelines) {
            auto &pipeline = it.second;

            if (pipeline->modified) {
                pipeline->modified = false;
                if (pipeline->pipeline != VK_NULL_HANDLE) {
                    // @todo: when is it safe to destroy a pipeline?
                    // vkDestroyPipeline(gpu->device, pipeline->pipeline, nullptr);
                    pipeline->pipeline = VK_NULL_HANDLE;
                }

                // @todo: make this configurable:
                // it has to be here for now as we need to shuffle the pointers around.
                VkPipelineColorBlendAttachmentState colorBlendAttachment{};
                colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                colorBlendAttachment.blendEnable = VK_FALSE;
                colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
                colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
                colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
                colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
                colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
                colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

                VkPipelineColorBlendStateCreateInfo colorBlending{};
                colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                colorBlending.logicOpEnable = VK_FALSE;
                colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
                colorBlending.attachmentCount = 1;
                colorBlending.pAttachments = &colorBlendAttachment;
                colorBlending.blendConstants[0] = 0.0f; // Optional
                colorBlending.blendConstants[1] = 0.0f; // Optional
                colorBlending.blendConstants[2] = 0.0f; // Optional
                colorBlending.blendConstants[3] = 0.0f; // Optional

                VkGraphicsPipelineCreateInfo pipeline_info{};
                pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

                pipeline_info.stageCount = pipeline->config.shader->stages.size();
                pipeline_info.pStages = pipeline->config.shader->stages.data();


                VkPipelineVertexInputStateCreateInfo vertex_input_info{};
                {
                    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                    vertex_input_info.vertexBindingDescriptionCount = pipeline->vertex_input_info.bindings.size();
                    vertex_input_info.pVertexBindingDescriptions = pipeline->vertex_input_info.bindings.data();
                    vertex_input_info.vertexAttributeDescriptionCount = pipeline->vertex_input_info.attributes.size();
                    vertex_input_info.pVertexAttributeDescriptions = pipeline->vertex_input_info.attributes.data();

                    pipeline_info.pVertexInputState = &vertex_input_info;
                }

                VkPipelineDynamicStateCreateInfo dynamic_state{};
                {
                    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                    dynamic_state.dynamicStateCount = pipeline->dynamic_states.size();
                    dynamic_state.pDynamicStates = pipeline->dynamic_states.data();
                }

                pipeline_info.pInputAssemblyState = &pipeline->config.input_assembly;
                pipeline_info.pViewportState = &pipeline->config.viewport_state;
                pipeline_info.pRasterizationState = &pipeline->config.rasterizer;
                pipeline_info.pMultisampleState = &pipeline->config.multisampling;
                pipeline_info.pDepthStencilState = nullptr;
                pipeline_info.pColorBlendState = &colorBlending;
                pipeline_info.pDynamicState = &dynamic_state;
                pipeline_info.layout = pipeline->config.pipeline_layout;
                pipeline_info.renderPass = pipeline->config.render_pass;
                pipeline_info.subpass = 0;
                pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

                if (vkCreateGraphicsPipelines(gpu->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline->pipeline) != VK_SUCCESS) {
                    gpu_log.error("failed to create graphics pipeline");
                }
            }
        }
    }

    std::unordered_map<shader_program_load_params_t, ref_t<shader_program_t>> loaded_shaders;
    std::unordered_map<pipeline_config_t, ref_t<gpu_pipeline_t>> loaded_pipelines;

    fswatcher_t watcher;
    gpu_t *gpu;
};

loader_t g_loader;

struct imgui_renderer_state_t {
    VkCommandBuffer cmds;
};

void imgui_init(gpu_t &gpu) {
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // setup implot style
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, {0.15,0.15,0.15,0.0});

    VkDescriptorPool descriptor_pool;
    {
        VkDescriptorPoolSize imgui_pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = array_size(imgui_pool_sizes);
        pool_info.pPoolSizes = imgui_pool_sizes;
        if (vkCreateDescriptorPool(gpu.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool");
        }
    }

    // create unique gpu stuffs for imgui...
    ImGui_ImplGlfw_InitForVulkan(gpu.window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = gpu.instance;
    init_info.PhysicalDevice = gpu.pdev;
    init_info.Device = gpu.device;
    init_info.QueueFamily = gpu.queue_families.graphics,
    init_info.Queue = gpu.graphics_queue,
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = descriptor_pool;
    init_info.RenderPass = gpu.display_render_pass;
    init_info.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = gpu.swapchain.images.size();
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = VK_NULL_HANDLE;
    init_info.CheckVkResultFn = nullptr;
    ImGui_ImplVulkan_Init(&init_info);
}

#include <time.h>

struct cpu_timer_t {
    cpu_timer_t() {
        memset(measures, 0, sizeof(measures));
        measure_idx = 0;
    }
    void start() {
        timespec time1;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time1);

        start_time = time1.tv_sec * 1e9 + time1.tv_nsec;
    }

    void stop() {
        timespec time2;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time2);

        // f32 time_ns = (time2.tv_sec * 1e9 + time2.tv_nsec - start_time);
        f32 time_ms = (time2.tv_sec * 1e3 + time2.tv_nsec / 1e6 - start_time / 1e6);

        measures[measure_idx] = time_ms;
        measure_idx = (measure_idx + 1) % array_size(measures);
    }

    float measure_ns() {
        i64 idx = (i64)measure_idx - 1;
        if (idx < 0) {
            idx = array_size(measures) - 1;
        }
        return measures[idx];
    }

    u32 size() {
        return array_size(measures);
    }

    u64 start_time = 0;
    f32 measures[256];
    u32 measure_idx = 0;
};

// @todo: aaaah correctness!!!
struct gpu_timer_t {
    void init(gpu_t &gpu) {
        VkQueryPoolCreateInfo query_pool_info = {};
        query_pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query_pool_info.queryCount = 2;
        VK_CHECK(vkCreateQueryPool(gpu.device, &query_pool_info, nullptr, &query_pool));
    }

    void reset(gpu_t &gpu, VkCommandBuffer &cmds) {
        if (!is_first) vkGetQueryPoolResults(
           	gpu.device,
           	query_pool,
           	0,
           	2,
           	4 * sizeof(u64),
           	timestamps,
           	2 * sizeof(u64),
           	VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        is_first = false;

        vkCmdResetQueryPool(cmds, query_pool, 0, 2);
    }

    void start(gpu_t &gpu, VkCommandBuffer &cmds) {
        if (timestamps[1] != 0 && timestamps[3] != 0) {
            auto as_nanos = (timestamps[2] - timestamps[0]) / gpu.limits.timestamp_period;
            measures[measure_idx] = (f32)as_nanos / 1e6;
            measure_idx = (measure_idx + 1) % array_size(measures);
        }

        vkCmdWriteTimestamp(cmds, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 0);
    }
    void stop(gpu_t &gpu, VkCommandBuffer &cmds) {
        vkCmdWriteTimestamp(cmds, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 1);

        (void)gpu;
    }

    u64 timestamps[4] = {0};
    f32 measures[256] = {0};
    u32 measure_idx = 0;
    bool is_first = true;
    VkQueryPool query_pool;
};

int main(void) {
    oc_init();
    glfwInit();

    modimp::scene_t scene_test;
    {
        auto res = modimp::scene_load(scene_test, "assets/cube.obj");
        if (res.is_err()) {
            g_log.error("failed to load model: {}", res.unwrap_err());
            return 1;
        }

        g_log.info("loaded model");
        g_log.info("   num meshes: {}", scene_test.meshes.len);

        for (auto &m : scene_test.meshes) {
            g_log.info("   mesh");
            g_log.info("   num vertices: {}", m.vertices.len);
            g_log.info("   num indices: {}", m.indices.len);
            g_log.info("   bounds: {}", m.bounds);
        }
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1200, 900, "vulkan", nullptr, nullptr);

    // initialize vulkan
    gpu_t gpu;
    gpu.init(window);
    g_log.info("gpu initialized");

    g_loader.init(gpu);

    imgui_init(gpu);
    g_log.info("imgui initialized");

    // setup the camera ubo
    struct camera_uniform_t {
        m4f view;
        m4f proj;
    };

    // create a test pipeline and pass
    ref_t<gpu_pipeline_t> pipeline;
    {
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE; // useful to set as true for shadow mapping
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f; // Optional
        rasterizer.depthBiasClamp = 0.0f; // Optional
        rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f; // Optional
        multisampling.pSampleMask = nullptr; // Optional
        multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
        multisampling.alphaToOneEnable = VK_FALSE; // Optional

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f; // Optional
        colorBlending.blendConstants[1] = 0.0f; // Optional
        colorBlending.blendConstants[2] = 0.0f; // Optional
        colorBlending.blendConstants[3] = 0.0f; // Optional

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 0; // Optional
        pipelineLayoutInfo.pSetLayouts = nullptr; // Optional
        pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
        pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

        VkPipelineLayout pipelineLayout;
        VK_CHECK(vkCreatePipelineLayout(gpu.device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

        auto shader = g_loader.load_shader_program({
           .vertex_hlsl_path = "eassets/shaders/test.vert",
           .fragment_hlsl_path = "eassets/shaders/test.frag",
        });

        // @todo: It would be fun to try to de-interlace the properties.
        //
        // Buffer1:
        //  - position v3f
        //  - normals v3f
        //  - uv v2f
        // Buffer2: index
        pipeline = g_loader.make_pipeline({
            .shader = shader,
            .vertex_input_info = {
                .bindings = {
                    {
                        .binding = 0,
                        .stride = 8 * sizeof(f32),
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                    }
                },
                .attributes = {
                    {
                        .location = 0,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = 0,
                    },
                    {
                        .location = 1,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = 3 * sizeof(f32),
                    },
                    {
                        .location = 2,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32_SFLOAT,
                        .offset = 6 * sizeof(f32),
                    }

                },
            },
            .input_assembly = inputAssembly,
            .viewport_state = viewportState,
            .rasterizer = rasterizer,
            .multisampling = multisampling,
            .dynamic_state = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
            },
            .pipeline_layout = pipelineLayout,
            .render_pass = gpu.display_render_pass,
        });
    }

    // setup the command-buffers.
    // i do not think these must be owned by the gpu.

    VkCommandBuffer cmds;
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = gpu.command_pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VK_CHECK(vkAllocateCommandBuffers(gpu.device, &allocInfo, &cmds));
    }

    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;

    // create sync objects
    {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VK_CHECK(vkCreateSemaphore(gpu.device, &semaphoreInfo, nullptr, &imageAvailableSemaphore));
        VK_CHECK(vkCreateSemaphore(gpu.device, &semaphoreInfo, nullptr, &renderFinishedSemaphore));
        VK_CHECK(vkCreateFence(gpu.device, &fenceInfo, nullptr, &inFlightFence));
    }

    cpu_timer_t full_loop_timer;
    gpu_timer_t full_frame_timer;
    gpu_timer_t imgui_render_timer;

    full_frame_timer.init(gpu);
    imgui_render_timer.init(gpu);

    vkDeviceWaitIdle(gpu.device);

    // create a gpu buffer
    VkBuffer vertex_buffer;
    VmaAllocation vertex_buffer_alloc;
    VkBuffer index_buffer;
    VmaAllocation index_buffer_alloc;
    {
        // we can extract the model from the test_scene first.
        auto &m = scene_test.meshes[0];

        // @todo: use a staging buffer for this.
        // upload into a buffer with TRANSFER_SRC, and move into a buffer with TRANSFER_DST.
        // this would be more optimal.
        {

            auto size = 8 * sizeof(f32) * m.vertices.len;
            auto buffer = new u8[size];
            auto vertices = slice<u8>((u8 *)buffer, size);
            for (usize i = 0; i < m.vertices.len; i++) {
                auto &v = m.vertices[i];
                auto &n = m.normals[i];
                auto &uv = m.texcoords[0][i];

                f32 *ptr = (f32 *)buffer + i * 8;
                ptr[0] = v.x;
                ptr[1] = v.y;
                ptr[2] = v.z;
                ptr[3] = n.x;
                ptr[4] = n.y;
                ptr[5] = n.z;
                ptr[6] = uv.x;
                ptr[7] = uv.y;
            }

            gpu.create_buffer_persistent(vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                vertex_buffer, vertex_buffer_alloc);
        }

        {
            auto indices = slice<u32>((u32 *)m.indices.data, m.indices.len);
            gpu.create_buffer_persistent(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                index_buffer, index_buffer_alloc);
        }
    }

    g_log.info("running...");
    while(!glfwWindowShouldClose(window)) {
        g_loader.process_hotreload();
        full_loop_timer.start();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        static bool show_imgui_demo = false;
        static bool show_implot_demo = false;
        if (show_imgui_demo)
            ImGui::ShowDemoWindow(&show_imgui_demo);

        if (show_implot_demo)
            ImPlot::ShowDemoWindow(&show_implot_demo);

        ImGui::Begin("test");

        {
            if (ImPlot::BeginPlot("Frame Times")) {
                ImPlot::SetupAxes("Frame", "Time (ms)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
                ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);
                ImPlot::PlotLine("Loop", full_loop_timer.measures, array_size(full_loop_timer.measures));
                ImPlot::PlotLine("Frame", full_frame_timer.measures, array_size(full_frame_timer.measures));
                ImPlot::PlotLine("ImGui", imgui_render_timer.measures, array_size(imgui_render_timer.measures));

                ImPlot::EndPlot();
            }
        }

        if (ImGui::Button("Show ImGui demo")) show_imgui_demo = !show_imgui_demo;
        if (ImGui::Button("Show ImPlot demo")) show_implot_demo = !show_implot_demo;

        ImGui::End();
        ImGui::Render();

        // draw frame
        // @todo: support multiple frames in flight.
        {
            VK_CHECK(vkWaitForFences(gpu.device, 1, &inFlightFence, VK_TRUE, UINT64_MAX));
            VK_CHECK(vkResetFences(gpu.device, 1, &inFlightFence));

            u32 image_idx;
            auto swapchain_result = vkAcquireNextImageKHR(gpu.device, gpu.swapchain.handle, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &image_idx);
            {
                // @note: we can also do || swapchain_result == VK_SUBOPTIMAL_KHR here,
                // but I'm not sure it has a big impact. On my machine, this causes swapchain recreation
                // every time i move ANY window.
                if (swapchain_result == VK_ERROR_OUT_OF_DATE_KHR) {
                    vkDeviceWaitIdle(gpu.device);
                    gpu.recreate_swapchain();

                    // signal the fence to avoid waiting for it.
                    vkQueueSubmit(gpu.graphics_queue, 0, nullptr, inFlightFence);

                    continue;
                } if (swapchain_result == VK_SUBOPTIMAL_KHR) {

                } else VK_CHECK(swapchain_result);
            }

            VkViewport full_viewport{};
            full_viewport.x = 0.0f;
            full_viewport.y = 0.0f;
            full_viewport.width = (float) gpu.swapchain.extent.width;
            full_viewport.height = (float) gpu.swapchain.extent.height;
            full_viewport.minDepth = 0.0f;
            full_viewport.maxDepth = 1.0f;

            VkRect2D full_scissor{};
            full_scissor.offset = {0, 0};
            full_scissor.extent = gpu.swapchain.extent;

            VK_CHECK(vkResetCommandBuffer(cmds, 0));
            // use the command buffer
            {
                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = 0; // Optional
                beginInfo.pInheritanceInfo = nullptr; // Optional

                VK_CHECK(vkBeginCommandBuffer(cmds, &beginInfo));
            }
            full_frame_timer.reset(gpu, cmds);
            imgui_render_timer.reset(gpu, cmds);

            full_frame_timer.start(gpu, cmds);

            // begin render pass
            {
                VkRenderPassBeginInfo renderPassInfo{};
                renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassInfo.renderPass = gpu.display_render_pass;
                renderPassInfo.framebuffer = gpu.swapchain.framebuffers[image_idx];
                renderPassInfo.renderArea.offset = {0, 0};
                renderPassInfo.renderArea.extent = gpu.swapchain.extent;

                VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
                renderPassInfo.clearValueCount = 1;
                renderPassInfo.pClearValues = &clearColor;

                vkCmdBeginRenderPass(cmds, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            }

            vkCmdBindPipeline(cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

            VkBuffer buffers[] = {vertex_buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmds, 0, array_size(buffers), buffers, offsets);
            vkCmdBindIndexBuffer(cmds, index_buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdSetViewport(cmds, 0, 1, &full_viewport);
            vkCmdSetScissor(cmds, 0, 1, &full_scissor);
            vkCmdDrawIndexed(cmds, scene_test.meshes[0].indices.len, 1, 0, 0, 0);

            imgui_render_timer.start(gpu, cmds);
            ImDrawData* draw_data = ImGui::GetDrawData();
            ImGui_ImplVulkan_RenderDrawData(draw_data, cmds);
            imgui_render_timer.stop(gpu, cmds);

            vkCmdEndRenderPass(cmds);

            full_frame_timer.stop(gpu, cmds);
            VK_CHECK(vkEndCommandBuffer(cmds));

            // submit command buffer
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

            VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
            submitInfo.pWaitDstStageMask = waitStages;

            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmds;

            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &renderFinishedSemaphore;

            VK_CHECK(vkQueueSubmit(gpu.graphics_queue, 1, &submitInfo, inFlightFence));

            // present
            VkPresentInfoKHR presentInfo{};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &renderFinishedSemaphore;

            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &gpu.swapchain.handle;
            presentInfo.pImageIndices = &image_idx;
            presentInfo.pResults = nullptr;

            vkQueuePresentKHR(gpu.present_queue, &presentInfo);
        }

        glfwPollEvents();

        full_loop_timer.stop();
    }

    glfwDestroyWindow(window);

    glfwTerminate();
}
