#include "shader_compiler.h"
#include "log.h"
#include "oc.h"

#include <spirv/spirv.h>

static logger_t logger = logger_t("shader_compiler");

VkShaderStageFlagBits get_shader_stage(spv::ExecutionModel executionModel);

void parse_spirv(slice<u32> code, VkShaderStageFlagBits &stage);

Shader::Shader(std::vector<ShaderModule> modules) {
    for (auto &module : modules) {
        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = module.stages();
        stageInfo.module = module.module();
        stageInfo.pName = "main";
        m_stages.push_back(stageInfo);
    }
}

void Shader::apply_to(VkGraphicsPipelineCreateInfo &info) const {
    info.stageCount = m_stages.size();
    info.pStages = m_stages.data();
}
void Shader::apply_to(VkComputePipelineCreateInfo &info) const {
    info.stage = m_stages[0];
}

ShaderCompiler::ShaderCompiler(gpu_t &gpu, const char *glslc_path)
: m_gpu(&gpu) {
    m_glslc_path = glslc_path;
}

ShaderModule ShaderCompiler::compile(const char *glsl_path) {
    u64 hash = std::hash<std::string>{}(glsl_path);

    const char *tmp_dir = "/tmp";
    auto tmp_path = fmt::format("{}/{}.spv", tmp_dir, hash);

    const char *debug_info = m_emit_debug_info ? "-g" : "";

    // @todo: make it easy to set flags and stuff

    auto cmd = fmt::format("{} {} --target-env=vulkan1.3 -O -o {} {}", m_glslc_path,
        debug_info, tmp_path, glsl_path);

    // @todo: use exec instead of system.
    // we want to be able to do these things in parallel i think.
    // for this, we will break this function into two, (try_invoke_compiler, create_shader),
    // and then we can call try_invoke_compiler in parallel.
    system(cmd.c_str());

    auto code = file_read(tmp_path.c_str()).unwrap();

    // introspect the code to determine the shader stage
    // we could do cool things in the future (like sharing a single module for multiple stages)
    VkShaderStageFlagBits stage;
    slice<u32> code_as_u32 = slice<u32>((u32 *)code.contents.data, code.contents.len / sizeof(u32));
    parse_spirv(code_as_u32, stage);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.contents.len;
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.contents.data);

    VkShaderModule shader_module;
    if (vkCreateShaderModule(m_gpu->device, &createInfo, nullptr, &shader_module) != VK_SUCCESS) {
        logger.error("failed to create shader module");
    }

    file_close(code);

    return ShaderModule{stage, shader_module};
}

void parse_spirv(slice<u32> code, VkShaderStageFlagBits &stage) {
    assert(code[0] == spv::MagicNumber);

    u32 *instr = code.data + 5;
    while (instr < code.end()) {
        u16 word_count = (instr[0] >> 16);
        u16 opcode = instr[0];

        switch (opcode) {
        case spv::OpEntryPoint: {
            assert(word_count >= 2);
            u16 executionModel = instr[1];
            stage = get_shader_stage((spv::ExecutionModel)executionModel);
            return;
        }
        }

        instr += word_count;
    }
}

VkShaderStageFlagBits get_shader_stage(spv::ExecutionModel executionModel) {
    switch (executionModel)
	{
	case spv::ExecutionModelVertex:
		return VK_SHADER_STAGE_VERTEX_BIT;
	case spv::ExecutionModelGeometry:
		return VK_SHADER_STAGE_GEOMETRY_BIT;
	case spv::ExecutionModelFragment:
		return VK_SHADER_STAGE_FRAGMENT_BIT;
	case spv::ExecutionModelGLCompute:
		return VK_SHADER_STAGE_COMPUTE_BIT;

	default:
		assert(!"Unsupported execution model");
		return VkShaderStageFlagBits(0);
	}
}
