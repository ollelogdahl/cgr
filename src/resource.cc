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

    // lazy-load: compile later.
    // std::vector<VkPipelineShaderStageCreateInfo> stages;
    // stages.push_back(compile_shader(*gpu, params.vertex_hlsl_path, SHADER_STAGE_VERTEX));
    // stages.push_back(compile_shader(*gpu, params.fragment_hlsl_path, SHADER_STAGE_FRAGMENT));

    loaded_shaders[params] = make_ref<shader_program_t>();
    shader_program_t &program = *loaded_shaders[params];
    program.stages = std::vector<VkPipelineShaderStageCreateInfo>();
    program.params = params;
    program.modified = true;

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

void loader_t::init(gpu_t &gpu) {
    this->gpu = &gpu;
    watcher.init();
}

void loader_t::process_hotreload() {
    watcher.process_watches();

    for (auto it : loaded_shaders) {
        auto &program = it.second;
        if (program->modified) {
            program->stages.clear();

            program->stages.push_back(compile_shader(*gpu, program->params.vertex_hlsl_path, SHADER_STAGE_VERTEX));
            program->stages.push_back(compile_shader(*gpu, program->params.fragment_hlsl_path, SHADER_STAGE_FRAGMENT));
        }
    }

    gpu->rebuild_pipelines();

    for (auto it : loaded_shaders) {
        auto &program = it.second;
        program->modified = false;
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
