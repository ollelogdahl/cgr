#include "cvar.h"
#include <deque>
#include <variant>
#include <algorithm>

namespace cvar {

// deque used for pointer stability.
static std::vector<CVar> cvars;

Bool declare_bool(const char *name, const char *description, bool default_value) {
    Bool b;
    auto var = cvars.emplace_back(CVar{
        .type = CVar::Type::Bool,
        .name = name,
        .description = description,
        .value = default_value
    });
    b.m_value = &std::get<bool>(var.value);
    return b;
}

Uint declare_uint(const char *name, const char *description, u64 default_value) {
    Uint u;
    auto var = cvars.emplace_back(CVar{
        .type = CVar::Type::Uint,
        .name = name,
        .description = description,
        .value = default_value
    });
    u.m_value = &std::get<u64>(var.value);
    return u;
}

Bool get_bool(const char *name) {
    auto cvar = std::find_if(cvars.begin(), cvars.end(), [&](const CVar &cvar) {
        return cvar.name == name && cvar.type == CVar::Type::Bool;
    });
    if (cvar != cvars.end()) {
        Bool b;
        b.m_value = &std::get<bool>(cvar->value);
        return b;
    }
    return Bool{};
}

Uint get_uint(const char *name) {
    auto cvar = std::find_if(cvars.begin(), cvars.end(), [&](const CVar &cvar) {
        return cvar.name == name && cvar.type == CVar::Type::Uint;
    });
    if (cvar != cvars.end()) {
        Uint u;
        u.m_value = &std::get<u64>(cvar->value);
        return u;
    }
    return Uint{};
}

std::vector<CVar> get_all() {
    return cvars;
}

}
