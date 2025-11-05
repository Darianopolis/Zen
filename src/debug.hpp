#pragma once

#include "pch.hpp"
#include "log.hpp"

// -----------------------------------------------------------------------------

std::string duration_to_string(std::chrono::duration<double, std::nano> dur);

// -----------------------------------------------------------------------------

struct FunctionTrace
{
    std::chrono::steady_clock::time_point enter_time;
    const char* name;

    static thread_local int depth;

    FunctionTrace(const char* extra = nullptr, std::source_location loc = std::source_location::current())
        : enter_time(std::chrono::steady_clock::now())
        , name(extra ? extra : loc.function_name())
    {
        depth++;
    }

    ~FunctionTrace()
    {
        depth--;
        auto leave_time = std::chrono::steady_clock::now();
        if (leave_time - enter_time > 1ms) {
            log_warn("{}{} - {}", std::string(depth * 2, ' '), name ?: "", duration_to_string(leave_time - enter_time));
        }
    }
};

// -----------------------------------------------------------------------------

struct FrameTimeReporter
{
    uint32_t frame_count = 0;
    std::chrono::steady_clock::time_point last_frame_done;
    std::chrono::steady_clock::time_point last_report;
    std::chrono::steady_clock::duration   longest_frametime;
    std::chrono::steady_clock::duration   shortest_frametime;
    std::chrono::steady_clock::duration   total_frametime;

    static constexpr auto report_interval = 0.5s;

    void frame(std::string_view name);
};

// -----------------------------------------------------------------------------

struct Surface;

void surface_profiler_report_commit(Surface* surface);
