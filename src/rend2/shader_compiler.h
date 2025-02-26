#pragma once

#include "gpu.h"
#include <vulkan/vulkan_core.h>

class ShaderCompiler;

class ShaderModule {
public:
    VkShaderStageFlagBits stages() const { return m_stage; }
    VkShaderModule module() const { return m_module; }
private:
    ShaderModule(VkShaderStageFlagBits stage, VkShaderModule module)
    : m_stage(stage), m_module(module) {}

    friend class ShaderCompiler;

    VkShaderStageFlagBits m_stage;
    VkShaderModule m_module;
};

class Shader {
public:
    Shader(std::vector<ShaderModule> modules);

    void apply_to(VkGraphicsPipelineCreateInfo &info) const;
    void apply_to(VkComputePipelineCreateInfo &info) const;
private:
    std::vector<VkPipelineShaderStageCreateInfo> m_stages;
};

class ShaderCompiler {
public:
    ShaderCompiler(gpu_t &gpu, const char *glslc_path);

    ShaderModule compile(const char *glsl_path);
private:
    gpu_t *m_gpu;
    const char *m_glslc_path;
    bool m_emit_debug_info = true;
};
