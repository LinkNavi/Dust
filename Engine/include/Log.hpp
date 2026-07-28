#pragma once
#include <cstdio>
#include <string_view>

namespace Zora {

enum class LogLevel { None = 0, Info = 1, Verbose = 2, Debug = 3 };

inline LogLevel g_log_level = LogLevel::None;

inline void set_log_level(int lvl) {
    g_log_level = static_cast<LogLevel>(lvl);
}

inline void log_info(std::string_view msg) {
    if (g_log_level >= LogLevel::Info)
        std::printf("[info]    %.*s\n", (int)msg.size(), msg.data());
}

inline void log_verbose(std::string_view msg) {
    if (g_log_level >= LogLevel::Verbose)
        std::printf("[verbose] %.*s\n", (int)msg.size(), msg.data());
}

inline void log_debug(std::string_view msg) {
    if (g_log_level >= LogLevel::Debug)
        std::printf("[debug]   %.*s\n", (int)msg.size(), msg.data());
}

inline void log_err(std::string_view msg) {
    std::fprintf(stderr, "[error]   %.*s\n", (int)msg.size(), msg.data());
}

} // namespace Zora
