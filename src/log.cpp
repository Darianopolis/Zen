#include "pch.hpp"
#include "log.hpp"

using namespace std::literals;

#define VT_color_begin(color) "\u001B[" #color "m"
#define VT_color_reset "\u001B[0m"
#define VT_color(color, text) VT_color_begin(color) text VT_color_reset

static struct {
    LogLevel log_level = LogLevel::trace;
    wlr_log_importance wlr_level = WLR_INFO;
    std::ofstream log_file;
} log_state = {};

LogLevel get_log_level()
{
    return log_state.log_level;
}

void log(LogLevel level, std::string_view message)
{
    if (log_state.log_level > level) return;

    struct {
        const char* vt;
        const char* plain;
    } fmt;

    switch (level) {
        case LogLevel::trace: fmt = { "[" VT_color(90, "TRACE") "] " VT_color(90, "{}") "\n", "[TRACE] {}\n" }; break;
        case LogLevel::debug: fmt = { "[" VT_color(96, "DEBUG") "] {}\n",                     "[DEBUG] {}\n" }; break;
        case LogLevel::info:  fmt = { " [" VT_color(94, "INFO") "] {}\n",                     " [INFO] {}\n" }; break;
        case LogLevel::warn:  fmt = { " [" VT_color(93, "WARN") "] {}\n",                     " [WARN] {}\n" }; break;
        case LogLevel::error: fmt = { "[" VT_color(91, "ERROR") "] {}\n",                     "[ERROR] {}\n" }; break;
        case LogLevel::fatal: fmt = { "[" VT_color(91, "FATAL") "] {}\n",                     "[FATAL] {}\n" }; break;
    }

    std::cout << std::vformat(fmt.vt, std::make_format_args(message));
    if (log_state.log_file.is_open()) {
        log_state.log_file << std::vformat(fmt.plain, std::make_format_args(message)) << std::flush;
    }
}

static
void log_wlr_callback(wlr_log_importance importance, const char* fmt, va_list args)
{
    if (log_state.wlr_level < importance) return;

    LogLevel level;
    switch (importance) {
        case WLR_ERROR: level = LogLevel::error; break;
        case WLR_INFO:  level = LogLevel::info;  break;
        case WLR_DEBUG: level = LogLevel::trace; break;
        default:        level = LogLevel::fatal; break;
    }

    // Cut off location formatting from wlroots message
    // TODO: Add location and timestamp formatting to our own logging

    std::string_view fmtv = fmt;
    if (fmtv.starts_with("[%s:%d]")) {
        va_arg(args, const char*);
        va_arg(args, int);
        fmt += "[%s:%d] "sv.length();
    }
    else if (fmtv.starts_with("[wayland]")) {
        fmt += "[wayland] "sv.length();
    }

    // Format and print

    char buffer[65'536];
    int len = vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
    buffer[std::min(len, int(sizeof(buffer)) - 1)] = '\0';

    log(level, buffer);
}

void init_log(LogLevel log_level, wlr_log_importance importance, const char* log_file)
{
    log_state.log_level = log_level;
    if (log_file) log_state.log_file = std::ofstream(log_file);

    log_state.wlr_level = importance;
    wlr_log_init(importance, log_wlr_callback);
}
