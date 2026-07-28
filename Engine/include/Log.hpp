#pragma once
#include <cstdio>
#include <string>
#include <string_view>
#include <sstream>
#include <type_traits>

namespace Dust {

enum class LogLevel { None = 0, Info = 1, Verbose = 2, Debug = 3 };
inline LogLevel g_log_level = LogLevel::None;
inline void set_log_level(int lvl) {
    g_log_level = static_cast<LogLevel>(lvl);
}

// ─── STRINGIFY ────────────────────────────────
// Priority: operator<< > to_string > raw print

template<typename T, typename = void>
struct has_ostream : std::false_type {};
template<typename T>
struct has_ostream<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<T>())>>
    : std::true_type {};

template<typename T>
std::string stringify(const T& val) {
    if constexpr (std::is_same_v<T, std::string>) {
        return val;
    } else if constexpr (std::is_same_v<T, std::string_view> || std::is_convertible_v<T, const char*>) {
        return std::string(val);
    } else if constexpr (std::is_same_v<T, bool>) {
        return val ? "true" : "false";
    } else if constexpr (std::is_arithmetic_v<T>) {
        return std::to_string(val);
    } else if constexpr (has_ostream<T>::value) {
        std::ostringstream ss;
        ss << val;
        return ss.str();
    } else {
        // fallback: print type + address
        std::ostringstream ss;
        ss << "[" << typeid(T).name() << " @ " << (const void*)&val << "]";
        return ss.str();
    }
}

// ─── VARIADIC CONCAT ──────────────────────────

inline std::string concat() { return ""; }

template<typename T, typename... Rest>
std::string concat(const T& first, const Rest&... rest) {
    return stringify(first) + concat(rest...);
}

// ─── LOG FUNCTIONS ────────────────────────────

template<typename... Args>
void log(Args&&... args) {
    if (g_log_level >= LogLevel::Info)
        std::printf("[info]    %s\n", concat(args...).c_str());
}

template<typename... Args>
void log_verbose(Args&&... args) {
    if (g_log_level >= LogLevel::Verbose)
        std::printf("[verbose] %s\n", concat(args...).c_str());
}

template<typename... Args>
void log_debug(Args&&... args) {
    if (g_log_level >= LogLevel::Debug)
        std::printf("[debug]   %s\n", concat(args...).c_str());
}

template<typename... Args>
void log_err(Args&&... args) {
    std::fprintf(stderr, "[error]   %s\n", concat(args...).c_str());
}

} // namespace Dust
