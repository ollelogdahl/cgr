#pragma once

#include "oc.h"
#include <span>
#include <variant>

namespace metrics {

void counter_reset(const char *name);
void counter_inc(const char *name);

void gauge(const char *name, u32 value);
void gauge(const char *name, f32 value);

struct Metric {
    enum class Type {
        Counter,
        Gauge,
    };
    Type type;
    const char *name;
    std::variant<u32, f32, u64> value;
};

std::span<const Metric> get_metrics();

}
