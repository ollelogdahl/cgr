#include "resource.h"
#include "log.h"

#include <sys/inotify.h>
#include <sys/stat.h>

#include <fcntl.h>
#include "modimp.h"
#include "vks.h"
#include <vulkan/vulkan_core.h>

#include <stb/stb_image.h>

#include <meshoptimizer/meshoptimizer.h>

#define SHADER_STAGE_VERTEX 0
#define SHADER_STAGE_FRAGMENT 1

static VkPipelineShaderStageCreateInfo compile_shader(const loader_t &loader, gpu_t &gpu, const char *path, int type);
static bool file_a_is_newer_than_b(const char *a, const char *b);

static logger_t logger = logger_t("loader");

ref_t<shader_program_t> loader_t::load_shader_program(const shader_program_load_params_t &params) {
    auto exists_it = loaded_shaders.find(params);
    if (exists_it != loaded_shaders.end()) {
        return exists_it->second;
    }

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

ref_t<texture_t> loader_t::load_texture(const texture_load_params_t &params) {
    auto exists_it = loaded_textures.find(params);
    if (exists_it != loaded_textures.end()) {
        return exists_it->second;
    }

    loaded_textures[params] = make_ref<texture_t>();
    texture_t &texture = *loaded_textures[params];
    {
        i32 width, height, channels;
        auto data = stbi_load(params.path, &width, &height, &channels, 0);
        if (!data) {
            auto cause = stbi_failure_reason();
            g_log.error("failed to load texture: {}: {}", params.path, cause);
            // @todo: return a 'default' texture.
        }

        VkFormat format;
        slice<byte> pixel_data;
        if (channels == 3 || channels == 4) {
            texture.info.channels = channels;
            format = VK_FORMAT_R8G8B8A8_UNORM;

            // the problem is that we need a r8b8g8a8 array to the gpu, but we only get
            // a 3-component from stb_image.
            // @todo: only when channels == 3
            byte *new_data = new byte[width * height * 4];
            for (auto i = 0; i < width * height; ++i) {
                new_data[i * 4 + 0] = data[i * 3 + 0];
                new_data[i * 4 + 1] = data[i * 3 + 1];
                new_data[i * 4 + 2] = data[i * 3 + 2];
                new_data[i * 4 + 3] = 255;
            }
            pixel_data = {new_data, (usize)(width * height * 4)};

        } else if (channels == 1) {
            texture.info.channels = 1;
            format = VK_FORMAT_R8_UNORM;
            pixel_data = {data, (usize)(width * height)};
        } else {
            format = VK_FORMAT_UNDEFINED;
            g_log.error("unsupported number of channels in texture: {}", channels);
        }

        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        gpu->create_image(pixel_data, (usize)width, (usize)height, format, usage, false, texture.image);

        VkImageViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = texture.image.image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(gpu->device, &view_info, nullptr, &texture.image.view));
    }

    return loaded_textures[params];
}

ref_t<model_t> loader_t::load_model(const model_load_params_t &params) {
    auto exists_it = loaded_models.find(params);
    if (exists_it != loaded_models.end()) {
        return exists_it->second;
    }

    loaded_models[params] = make_ref<model_t>();
    model_t &model = *loaded_models[params];

    modimp::scene_t scene;
    auto res = modimp::scene_load(scene, params.path);
    if (res.is_err()) {
        g_log.error("failed to load model: {}", res.unwrap_err());
        return nullptr;
    }

    logger.info("loaded model: {} with {} meshes", params.path, scene.meshes.len);

    const usize interleaved_vertex_size = 9 * sizeof(f32);
    auto interleave_attributes = [](slice<v3f> vertices, slice<u32> colors, slice<v3f> normals, slice<v2f> uvs, slice<u8> &out) {
        usize size = interleaved_vertex_size * vertices.len;
        out = slice<u8>((u8 *)malloc(size), size);

        for (usize i = 0; i < vertices.len; i++) {
            f32 *ptr = (f32 *)(out.data + i * interleaved_vertex_size);

            auto &v = vertices[i];
            ptr[0] = v.x;
            ptr[1] = v.y;
            ptr[2] = v.z;

            auto &n = normals[i];
            ptr[3] = n.x;
            ptr[4] = n.y;
            ptr[5] = n.z;

            if (colors.data != nullptr) {
                u32 *uptr = (u32 *)(ptr + 6);
                uptr[0] = colors[i];
            }

            if (uvs.data != nullptr) {
                auto &uv = uvs[i];
                ptr[7] = uv.x;
                ptr[8] = uv.y;
            }
        }
    };

    aabb_t model_aabb = aabb_t();
    for (auto &m : scene.meshes) {
        mesh_t mesh;
        mesh.bounds = m.bounds;
        model_aabb.include(m.bounds.min);
        model_aabb.include(m.bounds.max);

        mesh_t::lod_t lod_default;
        lod_default.lod_distance_sq = 0.0f;

        slice<byte> interleaved;
        interleave_attributes(m.vertices, m.colors, m.normals, m.texcoords, interleaved);
        slice<u32> indices = m.indices;
        usize vertex_count = m.vertices.len;

        /*
        meshopt_optimizeVertexCache(indices.data, indices.data, indices.len, interleaved.len);
        meshopt_optimizeOverdraw(indices.data, indices.data, indices.len, (f32 *)interleaved.data,
            vertex_count, interleaved_vertex_size, 1.05f);
        */

        gpu->create_buffer_persistent(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            lod_default.index_buffer);
        lod_default.index_count = indices.len;

        gpu->create_buffer_persistent(interleaved, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            mesh.vertex_buffer);

        logger.info("generating lod levels for model: {}", params.path);
        logger.info("    vertices: {}", vertex_count);
        mesh.lods.push_back(lod_default);

        usize i = 0;
        for (auto &lod : params.lod_settings) {
            i++;
            mesh_t::lod_t lod_new;
            lod_new.lod_distance_sq = lod.distance * lod.distance;

            auto lod_indices = std::vector<u32>();
            lod_indices.resize(indices.len);

            f32 lod_error = 0.0;

            usize new_len;
            if (lod.sloppy) {
                new_len = meshopt_simplifySloppy(&lod_indices[0], indices.data, indices.len,
                    (f32 *)interleaved.data, vertex_count, interleaved_vertex_size, 0,
                    lod.error_limit, &lod_error);
            } else {
                new_len = meshopt_simplify(&lod_indices[0], indices.data, indices.len,
                    (f32 *)interleaved.data, vertex_count, interleaved_vertex_size, 0,
                    lod.error_limit, 0, &lod_error);
            }

            lod_indices.resize(new_len);

            logger.info("    lod {} ({:.2}) (el: {:.2e}): {} (e: {:.2e})", i, lod.distance, lod.error_limit, new_len, lod_error);
            if (new_len == 0) {
                break;
            }

            gpu->create_buffer_persistent(slice<u32>(lod_indices.data(), lod_indices.size()), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                lod_new.index_buffer);
            lod_new.index_count = lod_indices.size();

            mesh.lods.push_back(lod_new);
        }

        model.meshes.push_back(make_ref<mesh_t>(mesh));
    }
    model.aabb = model_aabb;

    return loaded_models[params];
}

void loader_t::init(gpu_t &gpu) {
    this->gpu = &gpu;
    watcher.init();

    this->glslc_path = getenv("GLSLC_PATH");
    if (this->glslc_path == nullptr) {
        this->glslc_path = "glslc";
    }
    logger.info("using glslc: {}", this->glslc_path);
}

void loader_t::process_hotreload() {
    watcher.process_watches();

    for (auto it : loaded_shaders) {
        auto &program = it.second;
        if (program->modified) {
            program->stages.clear();

            program->stages.push_back(compile_shader(*this, *gpu, program->params.vertex_hlsl_path, SHADER_STAGE_VERTEX));
            program->stages.push_back(compile_shader(*this, *gpu, program->params.fragment_hlsl_path, SHADER_STAGE_FRAGMENT));
        }
    }

    gpu->rebuild_pipelines();

    for (auto it : loaded_shaders) {
        auto &program = it.second;
        program->modified = false;
    }
}

VkPipelineShaderStageCreateInfo compile_shader(const loader_t &loader, gpu_t &gpu, const char *path, int type) {
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
        auto cmd = fmt::format("{} -fshader-stage={} -o {} {}", loader.glslc_path, glslc_stage, tmp_path, path);

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
