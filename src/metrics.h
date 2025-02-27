#pragma once

#include "oc.h"
#include <span>
#include <variant>

namespace metrics {

void counter_reset(const char *name);
void counter_inc(const char *name);

void gauge_u32(const char *name, u32 value, const char *unit = "");
void gauge_f32(const char *name, f32 value, const char *unit = "");
void gauge_u64(const char *name, u64 value, const char *unit = "");

struct Metric {
    enum class Type {
        Counter,
        Gauge,
    };
    Type type;
    const char *name;
    const char *unit;
    std::variant<u32, f32, u64> value;
};

struct TreeNode {
    std::string name;
    std::vector<TreeNode> children;
    std::vector<Metric *> metrics;
};

const TreeNode &get_metric_tree();

}
