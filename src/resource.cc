#include "resource.h"
#include "log.h"

#include <sys/inotify.h>
#include <sys/stat.h>

#include <fcntl.h>
#include "modimp.h"
#include "vks.h"

#include "sg.h"

#include <vulkan/vulkan_core.h>

#include <stb/stb_image.h>

#include <meshoptimizer/meshoptimizer.h>

#define SHADER_STAGE_VERTEX 0
#define SHADER_STAGE_FRAGMENT 1

static VkPipelineShaderStageCreateInfo compile_shader(const loader_t &loader, const char *glslc_path, gpu_t &gpu, std::string path, int type);
static bool file_a_is_newer_than_b(const char *a, const char *b);

static logger_t logger = logger_t("loader");


// load param hash & equality
bool operator==(const texture_load_params_t &lhs, const texture_load_params_t &rhs) {
    return lhs.path == rhs.path && lhs.srgb == rhs.srgb && lhs.num_channels == rhs.num_channels;
}

std::size_t std::hash<texture_load_params_t>::operator()(const texture_load_params_t &params) const {
    return std::hash<std::string>{}(params.path) ^ std::hash<bool>{}(params.srgb) ^ std::hash<u32>{}(params.num_channels);
}

bool operator==(const shader_program_load_params_t &lhs, const shader_program_load_params_t &rhs) {
    // horrific code!!!
    {
        auto luse_glsl = !lhs.vertex_glsl_path.empty();
        auto ruse_glsl = !rhs.vertex_glsl_path.empty();

        // if one uses glsl, the other must also use glsl.
        if (luse_glsl != ruse_glsl) return false;

        if (luse_glsl) {
            if (lhs.vertex_glsl_path != rhs.vertex_glsl_path) return false;
        } else {
            if (lhs.vertex_spv_path != rhs.vertex_spv_path) return false;
        }
    }

    {
        auto luse_glsl = !lhs.fragment_glsl_path.empty();
        auto ruse_glsl = !rhs.fragment_glsl_path.empty();

        // if one uses glsl, the other must also use glsl.
        if (luse_glsl != ruse_glsl) return false;

        if (luse_glsl) {
            if (lhs.fragment_glsl_path != rhs.fragment_glsl_path) return false;
        } else {
            if (lhs.fragment_spv_path != rhs.fragment_spv_path) return false;
        }
    }

    return true;
}

std::size_t std::hash<shader_program_load_params_t>::operator()(const shader_program_load_params_t &params) const {
    std::size_t h = 0;
    if (!params.vertex_glsl_path.empty()) {
        h ^= std::hash<std::string>{}(params.vertex_glsl_path);
    } else {
        h ^= std::hash<std::string>{}(params.vertex_spv_path);
    }

    if (!params.fragment_glsl_path.empty()) {
        h ^= std::hash<std::string>{}(params.fragment_glsl_path);
    } else {
        h ^= std::hash<std::string>{}(params.fragment_spv_path);
    }

    return h;
}

bool operator==(const model_load_params_t &lhs, const model_load_params_t &rhs) {
    bool equal = true;
    equal = lhs.path == rhs.path;
    if (!equal) return false;
    equal = lhs.lod_settings.size() == rhs.lod_settings.size();
    if (!equal) return false;

    for (usize i = 0; i < lhs.lod_settings.size(); i++) {
        equal = lhs.lod_settings[i].error_limit == rhs.lod_settings[i].error_limit;
        if (!equal) return false;
    }

    return true;
}

std::size_t std::hash<model_load_params_t>::operator()(const model_load_params_t &params) const {
    std::size_t h = std::hash<std::string>{}(params.path);
    h ^= std::hash<u32>{}(params.lod_settings.size());
    for (usize i = 0; i < params.lod_settings.size(); i++) {
        h ^= std::hash<f32>{}(params.lod_settings[i].error_limit);
    }
    return h;
}

ref_t<gpu_shader_t> loader_t::load_shader_program(const shader_program_load_params_t &params) {
    auto exists_it = loaded_shaders.find(params);
    if (exists_it != loaded_shaders.end()) {
        return exists_it->second;
    }

    loaded_shaders[params] = make_ref<gpu_shader_t>();
    gpu_shader_t &program = *loaded_shaders[params];
    program.stages = std::vector<VkPipelineShaderStageCreateInfo>();
    program.params = params;
    program.modified = true;

    watcher.add_watch(params.vertex_glsl_path.c_str(), [](std::string, void *userdata) {
        auto *shader = static_cast<gpu_shader_t *>(userdata);
        shader->modified = true;
    }, &program);
    watcher.add_watch(params.fragment_glsl_path.c_str(), [](std::string, void *userdata) {
        auto *shader = static_cast<gpu_shader_t *>(userdata);
        shader->modified = true;
    }, &program);

    return loaded_shaders[params];
}

void load_texture_from_data(gpu_t &gpu, texture_t &texture, u32 width, u32 height, u32 channels, byte *data);

ref_t<texture_t> loader_t::load_texture(const texture_load_params_t &params) {
    auto exists_it = loaded_textures.find(params);
    if (exists_it != loaded_textures.end()) {
        return exists_it->second;
    }

    logger.info("loading texture: {}", params.path);
    loaded_textures[params] = make_ref<texture_t>();
    texture_t &texture = *loaded_textures[params];
    {
        i32 width, height, channels;
        auto data = stbi_load(params.path.c_str(), &width, &height, &channels, params.num_channels);
        if (!data) {
            auto cause = stbi_failure_reason();
            g_log.error("failed to load texture: {}: {}", params.path, cause);
            // @todo: return a 'default' texture.
        }

        load_texture_from_data(*gpu, texture, width, height, params.num_channels, data);
    }

    return loaded_textures[params];
}

model_description_t loader_t::load_model(const model_load_params_t &params) {
    // loading models is kinda special. The result of this operation is not
    // a reference to a model, but a description of where the model data is
    // located.
    //
    // Therefore, we need to check if the model is already loaded by checking
    // if all resources still exist.
    auto exists_it = loaded_models.find(params);
    if (exists_it != loaded_models.end()) {

        bool all_resources_exist = true;
        for (auto &mesh : exists_it->second.meshes) {
            if (!mesh.vertex_buffer.is_borrowed()) {
                all_resources_exist = false;
                break;
            }

            for (auto &lod : mesh.lods) {
                if (!lod.index_buffer.is_borrowed()) {
                    all_resources_exist = false;
                    break;
                }
            }
        }

        if (all_resources_exist)
            return exists_it->second;

        // we need to remove this entry and load the model again.
    }

    logger.info("loading model: {}", params.path);
    loaded_models[params] = model_description_t();
    model_description_t &model = loaded_models[params];

    modimp::scene_t scene;
    auto res = modimp::scene_load(scene, params.path.c_str());
    if (res.is_err()) {
        logger.error("failed to load model: {}", res.unwrap_err());
        return model;
    }

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
                // @note: we need to flip it, as rgba8 is stored as abgr8 on the cpu (little-endian)
                u32 flipped = ((colors[i] & 0xff000000) >> 24) | ((colors[i] & 0x00ff0000) >> 8) | ((colors[i] & 0x0000ff00) << 8) | ((colors[i] & 0x000000ff) << 24);
                uptr[0] = flipped;
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
        mesh_description_t mesh;
        mesh.bounds = m.bounds;
        model_aabb.include(m.bounds.min);
        model_aabb.include(m.bounds.max);

        mesh_description_t::lod_t lod_default;

        slice<byte> interleaved;
        interleave_attributes(m.vertices, m.colors, m.normals, m.texcoords, interleaved);
        slice<u32> indices = m.indices;
        usize vertex_count = m.vertices.len;

        meshopt_optimizeVertexCache(indices.data, indices.data, indices.len, interleaved.len);
        meshopt_optimizeOverdraw(indices.data, indices.data, indices.len, (f32 *)interleaved.data,
            vertex_count, interleaved_vertex_size, 1.05f);

        lod_default.index_buffer = make_ref<gpu_buffer_t>();
        mesh.vertex_buffer = make_ref<gpu_buffer_t>();

        gpu->create_buffer_persistent(indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            *lod_default.index_buffer);
        lod_default.index_count = indices.len;

        gpu->create_buffer_persistent(interleaved, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            *mesh.vertex_buffer);

        // logger.info("generating lod levels for model: {}", params.path);
        // logger.info("    vertices: {}", vertex_count);
        mesh.lods.push_back(lod_default);

        usize i = 0;
        for (auto &lod : params.lod_settings) {
            i++;
            mesh_description_t::lod_t lod_new;

            auto lod_indices = std::vector<u32>();
            lod_indices.resize(indices.len);

            f32 lod_error = 0.0;

            usize new_len = meshopt_simplify(&lod_indices[0], indices.data, indices.len,
                (f32 *)interleaved.data, vertex_count, interleaved_vertex_size, 0,
                lod.error_limit, 0, &lod_error);

            lod_indices.resize(new_len);

            (void)i;
            // logger.info("    lod {} (el: {:.2e}): {} (e: {:.2e})", i, lod.error_limit, new_len, lod_error);
            if (new_len == 0) {
                break;
            }

            lod_new.index_buffer = make_ref<gpu_buffer_t>();
            gpu->create_buffer_persistent(slice<u32>(lod_indices.data(), lod_indices.size()), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                *lod_new.index_buffer);
            lod_new.index_count = lod_indices.size();

            mesh.lods.push_back(lod_new);
        }

        model.meshes.push_back(mesh);
    }
    model.aabb = model_aabb;

    return loaded_models[params];
}

ref_t<sg::scene_t> loader_t::load_scene(const char *path) {
    auto exists_it = loaded_scenes.find(path);
    if (exists_it != loaded_scenes.end()) {
        return exists_it->second;
    }

    logger.info("loading scene: {}", path);
    loaded_scenes[path] = make_ref_owned<sg::scene_t>();
    sg::scene_t &scene = *loaded_scenes[path];
    scene.set_default_state(m_default_state);

    sg::load(*this, path, scene);

    watcher.add_watch(path, [](std::string path, void *userdata) {
        auto *scene = static_cast<sg::scene_t *>(userdata);
        scene->modified_on_disk = true;
        scene->disk_path = path;
    }, &scene);

    return loaded_scenes[path];
}

void loader_t::init(gpu_t &gpu) {
    this->gpu = &gpu;
    watcher.init();

    this->glslc_path = getenv("GLSLC_PATH");
    if (this->glslc_path == nullptr) {
        this->glslc_path = "glslc";
    }
    logger.info("using glslc: {}", this->glslc_path);

    {
        m_default_state = new sg::state_t();
        m_default_state->material = make_ref<material_t>();
        auto &mat = *m_default_state->material;
        // create the default material.
        mat.shader = load_shader_program({
            .vertex_glsl_path = "shaders/default.vert",
            .fragment_glsl_path = "shaders/default.frag",
        });
        mat.ambient = v4f{0.1, 0.1, 0.1, 1};
        mat.diffuse = v4f{0.8, 0.8, 0.8, 1};
        mat.specular = v4f{1, 1, 1, 1};
        mat.roughness = 0.07;
        mat.metallic = 0.0;
    }
}

void loader_t::process_hotreload() {
    watcher.process_watches();

    for (auto it : loaded_shaders) {
        auto &program = it.second;
        if (program->modified) {
            program->stages.clear();

            program->stages.push_back(compile_shader(*this, glslc_path, *gpu, program->params.vertex_glsl_path, SHADER_STAGE_VERTEX));
            program->stages.push_back(compile_shader(*this, glslc_path, *gpu, program->params.fragment_glsl_path, SHADER_STAGE_FRAGMENT));
        }
    }

    gpu->rebuild_pipelines();

    for (auto it : loaded_shaders) {
        auto &program = it.second;
        program->modified = false;
    }

    for (auto it : loaded_scenes) {
        auto &scene = it.second;
        if (scene->modified_on_disk) {
            g_log.info("reloading scene: {}", scene->disk_path);

            // loading scenes is special. They usually load many other
            // resources. Clearing it and then loading will cause
            // all unchanged resources to be reloaded. Not cool!

            sg::scene_t new_scene;
            new_scene.set_default_state(m_default_state);
            sg::load(*this, scene->disk_path.c_str(), new_scene);

            scene->clear();
            *scene = new_scene;
            scene->modified_on_disk = false;
        }
    }
}

VkPipelineShaderStageCreateInfo compile_shader(const loader_t &loader, const char *glslc_path, gpu_t &gpu, std::string path, int type) {
    // in dev mode, we compile the shader into the tmp dir. The filename in tmp is
    // based on the hash of the original file name.

    u64 hash = std::hash<std::string>{}(path);

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

    if (file_a_is_newer_than_b(path.c_str(), tmp_path.c_str())) {
        logger.info("compiling shader {}", path);

        bool emit_debug_info = true;
        const char *debug_info = emit_debug_info ? "-g" : "";

        // @todo: the path to glslc should maybe be compile-time configurable? or taken from env?
        auto cmd = fmt::format("{} {} -fshader-stage={} -o {} {}", glslc_path,
            debug_info, glslc_stage, tmp_path, path);

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
        panic("inotify_add_watch error: {}: {}", path, strerror(errno));
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

void load_texture_from_data(gpu_t &gpu, texture_t &texture, u32 width, u32 height, u32 channels, byte *data) {
    VkFormat format;
    slice<byte> pixel_data;

    if (channels == 4) {
        texture.info.channels = channels;
        format = VK_FORMAT_R8G8B8A8_UNORM;
        pixel_data = {data, (usize)(width * height * 4)};
    } else if (channels == 3) {
        texture.info.channels = channels;
        format = VK_FORMAT_R8G8B8_UNORM;

        pixel_data = {data, (usize)(width * height * 3)};
    } else if (channels == 1) {
        texture.info.channels = 1;
        format = VK_FORMAT_R8_UNORM;
        pixel_data = {data, (usize)(width * height)};
    } else {
        format = VK_FORMAT_UNDEFINED;
        logger.error("unsupported number of channels in texture: {}", channels);
    }

    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    gpu.create_image(pixel_data, (usize)width, (usize)height, format, usage, false, texture.image);

    // @todo: move this!
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

    VK_CHECK(vkCreateImageView(gpu.device, &view_info, nullptr, &texture.image.view));
}
