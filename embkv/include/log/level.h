#pragma once

namespace embkv::log::detail
{
enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

constexpr auto level_to_string(LogLevel level) noexcept -> const char* {
    switch (level) {
        case LogLevel::Debug:  return "[Debug]";
        case LogLevel::Info:   return "[Info]";
        case LogLevel::Warn:   return "[Warn]";
        case LogLevel::Error:  return "[Error]";
        case LogLevel::Fatal:  return "[Fatal]";
        default:               return "[Unknown]";
    }
}

} // namespace embkv::log::detail
