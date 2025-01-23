#include "resource.h"
#include "log.h"

#include <sys/inotify.h>
#include <sys/stat.h>

#include <fcntl.h>
#include "vks.h"
#include <vulkan/vulkan_core.h>

#define SHADER_STAGE_VERTEX 0
#define SHADER_STAGE_FRAGMENT 1

static VkPipelineShaderStageCreateInfo compile_shader(gpu_t &gpu, const char *path, int type);
static bool file_a_is_newer_than_b(const char *a, const char *b);

static logger_t logger = logger_t("loader");

ref_t<shader_program_t> loader_t::load_shader_program(const shader_program_load_params_t &params) {
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

ref_t<gpu_pipeline_t> loader_t::make_pipeline(const pipeline_config_t &config) {
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

    pipeline->color_attachment_formats = std::vector<VkFormat>(config.color_attachment_formats.len);
    memcpy(pipeline->color_attachment_formats.data(), config.color_attachment_formats.data, config.color_attachment_formats.len * sizeof(VkFormat));

    loaded_pipelines[config] = pipeline;

    return pipeline;
}

void loader_t::init(gpu_t &gpu) {
    this->gpu = &gpu;
    watcher.init();
}

void loader_t::process_hotreload() {
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

            VkPipelineRenderingCreateInfoKHR rendering_info{};
            rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
            rendering_info.colorAttachmentCount = pipeline->color_attachment_formats.size();
            rendering_info.pColorAttachmentFormats = pipeline->color_attachment_formats.data();
            rendering_info.depthAttachmentFormat = pipeline->config.depth_attachment_format;
            rendering_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

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

            VkPipelineViewportStateCreateInfo viewport_state{};
            {
                viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewport_state.viewportCount = 1;
                viewport_state.scissorCount = 1;
            }

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            {
                rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                rasterizer.depthClampEnable = VK_FALSE; // useful to set as true for shadow mapping
                rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
                rasterizer.lineWidth = 1.0f;
                rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
                rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
                rasterizer.depthBiasEnable = VK_FALSE;
                rasterizer.depthBiasConstantFactor = 0.0f; // Optional
                rasterizer.depthBiasClamp = 0.0f; // Optional
                rasterizer.depthBiasSlopeFactor = 0.0f; // Optional
            }

            VkPipelineColorBlendStateCreateInfo color_blending{};
            color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blending.logicOpEnable = VK_FALSE;
            color_blending.logicOp = VK_LOGIC_OP_COPY; // Optional
            color_blending.attachmentCount = 1;
            color_blending.pAttachments = &colorBlendAttachment;
            color_blending.blendConstants[0] = 0.0f; // Optional
            color_blending.blendConstants[1] = 0.0f; // Optional
            color_blending.blendConstants[2] = 0.0f; // Optional
            color_blending.blendConstants[3] = 0.0f; // Optional

            VkGraphicsPipelineCreateInfo pipeline_info{};
            pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

            pipeline_info.stageCount = pipeline->config.shader->stages.size();
            pipeline_info.pStages = pipeline->config.shader->stages.data();

            pipeline_info.pNext = &rendering_info;

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
            VkDynamicState dynamic_states[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
            };
            {
                dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamic_state.dynamicStateCount = 2;
                dynamic_state.pDynamicStates = dynamic_states;
            }

            VkPipelineDepthStencilStateCreateInfo depth_stencil_state{};
            {
                depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depth_stencil_state.depthTestEnable = pipeline->config.depth_stencil.depth_test;
                depth_stencil_state.depthWriteEnable = pipeline->config.depth_stencil.depth_write;
                depth_stencil_state.depthCompareOp = pipeline->config.depth_stencil.depth_compare_op;
                depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
                depth_stencil_state.minDepthBounds = 0.0f;
                depth_stencil_state.maxDepthBounds = 1.0f;
            }

            VkPipelineInputAssemblyStateCreateInfo input_assembly{};
            input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            input_assembly.primitiveRestartEnable = VK_FALSE;

            pipeline_info.pInputAssemblyState = &input_assembly;
            pipeline_info.pViewportState = &viewport_state;
            pipeline_info.pRasterizationState = &rasterizer;
            pipeline_info.pMultisampleState = &pipeline->config.multisampling;
            pipeline_info.pDepthStencilState = &depth_stencil_state;
            pipeline_info.pColorBlendState = &color_blending;
            pipeline_info.pDynamicState = &dynamic_state;
            pipeline_info.layout = pipeline->config.pipeline_layout;
            pipeline_info.subpass = 0;
            pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

            if (vkCreateGraphicsPipelines(gpu->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline->pipeline) != VK_SUCCESS) {
                logger.error("failed to create graphics pipeline");
            }
        }
    }
}

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
        logger.info("compiling shader {}", path);
        // @todo: the path to glslc should maybe be compile-time configurable? or taken from env?
        auto cmd = fmt::format("glslc -fshader-stage={} -o {} {}", glslc_stage, tmp_path, path);

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
        logger.error("failed to create shader module");
    }

    file_close(code);

    VkPipelineShaderStageCreateInfo stage_info{};
    stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_info.stage = vk_stage;
    stage_info.module = shader_module;
    stage_info.pName = "main";

    return stage_info;
}

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

bool fswatcher_t::elem_t::operator==(const fswatcher_t::elem_t &other) const {
    return path == other.path && on_modified == other.on_modified;
}

void fswatcher_t::init() {
    inotify_fd = inotify_init();
    auto old_flags = fcntl(inotify_fd, F_GETFL);
    fcntl(inotify_fd, F_SETFL, old_flags | O_NONBLOCK);
}

void fswatcher_t::add_watch(const char *path, void (*on_modified)(std::string, void *userdata), void *userdata) {
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
void fswatcher_t::process_watches() {
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
