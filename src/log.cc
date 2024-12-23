#include "log.h"

#include <iostream>

void _log_impl(const char *scope, u8 level, std::string message) {

    auto level_str = [&] {
        switch (level) {
        case 0: return "INFO";
        case 1: return "WARN";
        case 2: return "ERROR";
        default: return "UNKNOWN";
        }
    }();

    std::cout << "[" << scope << "] " << level_str << ": " << message << std::endl;
}

logger_t g_log = logger_t("main");
