#include "metrics.h"
#include <unordered_map>

namespace metrics {

static std::unordered_map<std::string, u32> g_lookup;
static std::vector<Metric> g_metrics;

inline constexpr Metric &get_of_type(const char *name, Metric::Type type, bool &created) {
    const char *type_as_str;
    switch (type) {
    case Metric::Type::Counter: type_as_str = "c:"; break;
    case Metric::Type::Gauge: type_as_str = "g:"; break;
    }

    std::string key = fmt::format("{}{}", type_as_str, name);

    if (auto it = g_lookup.find(key); it != g_lookup.end() && g_metrics[it->second].type == type) {
        return g_metrics[it->second];
    }

    created = true;
    g_lookup[key] = g_metrics.size();
    return g_metrics.emplace_back(Metric{type, name, (u32)0});
}

void counter_reset(const char *name) {
    bool created;
    auto &m = get_of_type(name, Metric::Type::Counter, created);
    m.value = (u64)0;
}

void counter_inc(const char *name) {
    bool created;
    auto &m = get_of_type(name, Metric::Type::Counter, created);
    if (created) {
        m.value = (u64)1;
    } else {
        m.value = std::get<u64>(m.value) + 1;
    }
}

void gauge(const char *name, u32 value) {
    bool created;
    auto &m = get_of_type(name, Metric::Type::Gauge, created);
    m.value = value;
}
void gauge(const char *name, f32 value) {
    bool created;
    auto &m = get_of_type(name, Metric::Type::Gauge, created);
    m.value = value;
}

std::span<const Metric> get_metrics() {
    return std::span<const Metric>(g_metrics);
}

}
