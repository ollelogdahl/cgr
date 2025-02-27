#include "metrics.h"
#include <deque>
#include <unordered_map>
#include <algorithm>

namespace metrics {

static std::unordered_map<std::string, u32> g_lookup;
static std::deque<Metric> g_metrics;
static TreeNode g_root;

inline Metric &get_of_type(const char *name, Metric::Type type, const char *unit, bool &created) {
    const char *type_as_str;
    switch (type) {
    case Metric::Type::Counter: type_as_str = "c:"; break;
    case Metric::Type::Gauge: type_as_str = "g:"; break;
    }

    std::string key = fmt::format("{}{}", type_as_str, name);

    if (auto it = g_lookup.find(key); it != g_lookup.end() && g_metrics[it->second].type == type) {
        return g_metrics[it->second];
    }

    std::vector<std::string_view> parts;
    std::string_view sv(name);
    while (!sv.empty()) {
        auto pos = sv.find('.');
        parts.push_back(sv.substr(0, pos));
        sv.remove_prefix(pos == sv.npos ? sv.size() : pos + 1);
    }

    auto real_name = parts.back();

    g_lookup[key] = g_metrics.size();
    Metric &metric = g_metrics.emplace_back(Metric{type, real_name.data(), unit, (u32)0});

    // create a tree
    {
        // lets remove the last part
        parts.pop_back();

        TreeNode *node = &g_root;
        for (const auto &part : parts) {
            auto it = std::find_if(node->children.begin(), node->children.end(), [&](const TreeNode &n) {
                return n.name == part;
            });
            if (it == node->children.end()) {
                node->children.push_back(TreeNode{std::string(part)});
                it = node->children.end() - 1;
            }
            node = &*it;
        }

        node->metrics.push_back(&metric);
    }

    created = true;
    return metric;
}

void counter_reset(const char *name) {
    bool created;
    auto &m = get_of_type(name, Metric::Type::Counter, "", created);
    m.value = (u64)0;
}

void counter_inc(const char *name) {
    bool created;
    auto &m = get_of_type(name, Metric::Type::Counter, "", created);
    if (created) {
        m.value = (u64)1;
    } else {
        m.value = std::get<u64>(m.value) + 1;
    }
}

void gauge_u32(const char *name, u32 value, const char *unit) {
    bool created;
    auto &m = get_of_type(name, Metric::Type::Gauge, unit, created);
    m.value = value;
}
void gauge_u64(const char *name, u64 value, const char *unit) {
    bool created;
    auto &m = get_of_type(name, Metric::Type::Gauge, unit, created);
    m.value = value;
}
void gauge_f32(const char *name, f32 value, const char *unit) {
    bool created;
    auto &m = get_of_type(name, Metric::Type::Gauge, unit, created);
    m.value = value;
}

const TreeNode &get_metric_tree() {
    return g_root;
}

}
