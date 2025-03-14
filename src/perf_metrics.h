#pragma once

#include "imgui/imgui.h"
#include "rend2/gpu_timer.h"
#include <limits>
#include <vector>
class PerfMetrics {
private:
    struct MetricStats {
        float min = std::numeric_limits<float>::max();
        float max = std::numeric_limits<float>::lowest();
        float max_in_history = std::numeric_limits<float>::lowest();
        float min_in_history = std::numeric_limits<float>::max();
        float avg = 0.0f;
        float current = 0.0f;
        std::vector<float> history;

        void update(float value) {
            current = value;
            min = std::min(min, value);
            max = std::max(max, value);

            // Keep running average
            if (history.size() >= 250) {
                history.erase(history.begin());
                max_in_history = std::numeric_limits<float>::lowest();
                min_in_history = std::numeric_limits<float>::max();
            }
            history.push_back(value);

            // Calculate average
            avg = 0.0f;
            for (float v : history) {
                avg += v;
                max_in_history = std::max(max_in_history, v);
                min_in_history = std::min(min_in_history, v);
            }
            avg /= history.size();
        }
    };

    MetricStats fps;
    MetricStats frame_time;
    MetricStats gpu_time; // If you have GPU timestamps

    GpuTimer *gpu_timer = nullptr;

    double last_time = 0.0;

public:
    PerfMetrics(gpu_t& gpu) {
        if (gpu.support.timestamp_queries) {
            gpu_timer = new GpuTimer(gpu);
        }
    }

    void begin_frame(VkCommandBuffer cmd, uint32_t frame_index) {
        if (gpu_timer) {
            gpu_timer->begin_frame(cmd, frame_index);
        }
    }

    void end_frame(VkCommandBuffer cmd) {
        if (gpu_timer) {
            gpu_timer->end_frame(cmd);
        }
    }

    void update() {
        double current_time = glfwGetTime();
        float delta = static_cast<float>(current_time - last_time);
        last_time = current_time;

        frame_time.update(delta * 1000.0f); // Convert to milliseconds
        fps.update(1.0f / delta);
    }

    void draw_gui() {
        ImGui::Begin("Performance Metrics");

        if (ImGui::CollapsingHeader("FPS", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("%5.1f (avg: %5.1f, min: %5.1f, max: %5.1f)", fps.current, fps.avg, fps.min_in_history, fps.max_in_history);

            // Draw FPS graph
            ImGui::PlotLines("FPS History",
                fps.history.data(),
                static_cast<int>(fps.history.size()),
                0, nullptr,
                fps.min_in_history * 0.9f,
                fps.max_in_history * 1.1f,
                ImVec2(0, 80));
        }

        if (ImGui::CollapsingHeader("Frame Time", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Current: %.2f ms", frame_time.current);
            ImGui::Text("Average: %.2f ms", frame_time.avg);
            ImGui::Text("Min: %.2f ms", frame_time.min_in_history);
            ImGui::Text("Max: %.2f ms", frame_time.max_in_history);

            ImGui::PlotLines("Frame Time History",
                frame_time.history.data(),
                static_cast<int>(frame_time.history.size()),
                0, nullptr,
                0.0f,
                frame_time.max_in_history * 1.1f,
                ImVec2(0, 80));
        }

        if (gpu_timer) {
            if (ImGui::CollapsingHeader("GPU Time", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Current: %.2f ms", gpu_time.current);
                ImGui::Text("Average: %.2f ms", gpu_time.avg);
                ImGui::Text("Min: %.2f ms", gpu_time.min_in_history);
                ImGui::Text("Max: %.2f ms", gpu_time.max_in_history);

                ImGui::PlotLines("GPU Time History",
                    gpu_time.history.data(),
                    static_cast<int>(gpu_time.history.size()),
                    0, nullptr,
                    0.0f,
                    gpu_time.max_in_history * 1.1f,
                    ImVec2(0, 80));
            }
        }

        ImGui::End();
    }
};
