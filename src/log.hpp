#pragma once

#include "pch.hpp"

enum class LogLevel : uint32_t
{
    trace,
    debug,
    info,
    warn,
    error,
    fatal,
};

void log_set_message_sink(struct MessageConnection*);
LogLevel get_log_level();
void init_log(LogLevel, wlr_log_importance, const char* log_file);
void      log(LogLevel, std::string_view message);

template<typename ...Args>
void log(LogLevel level, std::format_string<Args...> fmt, Args&&... args)
{
    if (get_log_level() > level) return;
    log(level, std::vformat(fmt.get(), std::make_format_args(args...)));
}

static constexpr std::string_view log_indent = "        ";

#define log_trace(fmt, ...) if (get_log_level() <= LogLevel::trace) log(LogLevel::trace, std::format(fmt __VA_OPT__(,) __VA_ARGS__))
#define log_debug(fmt, ...) if (get_log_level() <= LogLevel::debug) log(LogLevel::debug, std::format(fmt __VA_OPT__(,) __VA_ARGS__))
#define log_info( fmt, ...) if (get_log_level() <= LogLevel::info ) log(LogLevel::info,  std::format(fmt __VA_OPT__(,) __VA_ARGS__))
#define log_warn( fmt, ...) if (get_log_level() <= LogLevel::warn ) log(LogLevel::warn,  std::format(fmt __VA_OPT__(,) __VA_ARGS__))
#define log_error(fmt, ...) if (get_log_level() <= LogLevel::error) log(LogLevel::error, std::format(fmt __VA_OPT__(,) __VA_ARGS__))
