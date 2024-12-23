#pragma once

#include <fmt/core.h>
#include "oc.h"

struct logger_t {
public:
    inline constexpr logger_t() : m_scope("") {}
    inline constexpr logger_t(const char* scope) : m_scope(scope) {}

    template <class... Args>
    inline void info(fmt::format_string<Args...> fmt, Args&&... args);

    template <class... Args>
    inline void warn(fmt::format_string<Args...> fmt, Args&&... args);

    template <class... Args>
    inline void error(fmt::format_string<Args...> fmt, Args&&... args);
private:
    const char *m_scope;
};

void _log_impl(const char *scope, u8 level, std::string message);

template <class... Args>
inline void logger_t::info(fmt::format_string<Args...> fmt, Args&&... args) {
    _log_impl(m_scope, 0, fmt::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
inline void logger_t::warn(fmt::format_string<Args...> fmt, Args&&... args) {
    _log_impl(m_scope, 1, fmt::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
inline void logger_t::error(fmt::format_string<Args...> fmt, Args&&... args) {
    _log_impl(m_scope, 2, fmt::format(fmt, std::forward<Args>(args)...));
}

extern logger_t g_log;
