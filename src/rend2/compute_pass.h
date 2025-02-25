#pragma once

class ComputePass {
public:
    ComputePass(gpu_t &gpu, const char *name);

    void record(std::function<void(CommandBuffer &)> fn);
private:
    CommandBuffer m_cmd;
};
