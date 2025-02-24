#pragma once

// cvar system inspired by many engines

#include "oc.h"
#include <variant>
#include <vector>
namespace cvar {

class Bool {
public:
    void operator=(bool value) {
        if (m_value) {
            *m_value = value;
        }
    }
    operator bool() const {
        if (m_value) {
            return *m_value;
        }
        return false;
    }
private:
    bool *m_value = nullptr;

    friend Bool declare_bool(const char *name, const char *description, bool default_value);
    friend Bool get_bool(const char *name);
};

class Uint {
public:
    void operator=(bool value) {
        if (m_value) {
            *m_value = value;
        }
    }
    operator u64() const {
        if (m_value) {
            return *m_value;
        }
        return 0;
    }
private:
    u64 *m_value = nullptr;

    friend Uint declare_uint(const char *name, const char *description, u64 default_value);
    friend Uint get_uint(const char *name);
};

Bool declare_bool(const char *name, const char *description, bool default_value);
Uint declare_uint(const char *name, const char *description, u64 default_value);

struct CVar {
    enum class Type {
        Bool,
        Uint,
    };
    Type type;
    const char *name;
    const char *description;
    std::variant<bool, u64> value;
};

Bool get_bool(const char *name);
Uint get_uint(const char *name);

std::vector<CVar> get_all();

};
