#pragma

#include "pch.hpp"

// -----------------------------------------------------------------------------

std::string duration_to_string(std::chrono::duration<double, std::nano> dur);

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
