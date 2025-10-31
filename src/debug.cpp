#include "core.hpp"

std::string toplevel_to_string(Toplevel* toplevel)
{
    return toplevel
        ? std::format("Toplevel<{}>({}, {})",
            (void*)toplevel,
            toplevel->xdg_toplevel()->app_id ?: "?",
            toplevel->xdg_toplevel()->title ?: "?")
        : "nullptr";
}

std::string surface_to_string(Surface* surface)
{
    if (!surface) return "nullptr";
    switch (surface->role) {
        case SurfaceRole::toplevel:      return toplevel_to_string(Toplevel::from(surface));
        case SurfaceRole::popup:         return std::format("Popup<{}>", (void*)surface);
        case SurfaceRole::layer_surface: return std::format("LayerSurface<{}>", (void*)surface);
        case SurfaceRole::subsurface:    return std::format("Subsurface<{}>(parent = {})", (void*)surface, surface_to_string(Subsurface::from(surface)->parent()));
        default:
    }

    return std::format("InvalidSurface<{}>(role = {})", (void*)surface, std::to_underlying(surface->role));
}

std::string pointer_constraint_to_string(wlr_pointer_constraint_v1* constraint)
{
    return constraint
        ? std::format("PointerConstraint<{}>(type = {})", (void*)constraint, magic_enum::enum_name(constraint->type))
        : "nullptr";
}

std::string client_to_string(Client* client)
{
    if (!client) return "nullptr";

    return std::format("Client<{}>(pid = {}, name = {}, path = {})", (void*)client, client->pid, client->process_name, client->path.c_str());
}

std::string cursor_surface_to_string(CursorSurface* cursor_surface)
{
    if (!cursor_surface) return "nullptr";

    return std::format("CursorSurface<{}>(wlr_surface = {}, visible = {})", (void*)cursor_surface, (void*)cursor_surface->wlr_surface, cursor_surface_is_visible(cursor_surface));
}

std::string pointer_to_string(Pointer* pointer)
{
    return pointer ? std::format("Pointer<{}>", (void*)pointer) : "nullptr";
}

std::string output_to_string(Output* output)
{
    if (!output) return "nullptr";

    return std::format("Output<{}>(name = {}, desc = {}, pos = ({}, {}), size = ({}, {}), refresh = {:.2f}Hz, primary = {})",
        (void*)output,
        output->wlr_output->name,
        output->wlr_output->description,
        output->scene_output ? output->scene_output->x : output->config.pos.value_or({-1, -1}).x,
        output->scene_output ? output->scene_output->y : output->config.pos.value_or({-1, -1}).y,
        output->wlr_output->width,
        output->wlr_output->height,
        output->wlr_output->refresh / 1000.0,
        output->config.primary);
}

// -----------------------------------------------------------------------------

static
uint32_t decimals_for_3sf(double value)
{
    if (value < 10) return 2;
    if (value < 100) return 1;
    return 0;
}

static
std::string print_value_with_suffix(std::string_view suffix, double amount, uint32_t decimals)
{
    switch (decimals) {
        case 0: return std::format("{:.0f}{}", amount, suffix);
        case 1: return std::format("{:.1f}{}", amount, suffix);
        case 2: return std::format("{:.2f}{}", amount, suffix);
        default: std::unreachable();
    }
}

std::string duration_to_string(std::chrono::duration<double, std::nano> dur)
{
    double nanos = dur.count();

    if (nanos >= 1e9) {
        double seconds = nanos / 1e9;
        return print_value_with_suffix("s", seconds, decimals_for_3sf(seconds));
    } else if (nanos >= 1e6) {
        double millis = nanos / 1e6;
        return print_value_with_suffix("ms", millis, decimals_for_3sf(millis));
    } else if (nanos >= 1e3) {
        double micros = nanos / 1e3;
        return print_value_with_suffix("us", micros, decimals_for_3sf(micros));
    } else if (nanos >= 0) {
        return print_value_with_suffix("ns", nanos, decimals_for_3sf(nanos));
    } else {
        return "0";
    }
}

// -----------------------------------------------------------------------------

int thread_local FunctionTrace::depth = 0;

// -----------------------------------------------------------------------------

void FrameTimeReporter::frame(std::string_view name)
{
    auto now = std::chrono::steady_clock::now();
    frame_count++;
    auto ft = now - last_frame_done;
    total_frametime += ft;
    last_frame_done = now;
    longest_frametime = std::max(longest_frametime, ft);
    shortest_frametime = std::min(shortest_frametime, ft);
    if (now - last_report > report_interval) {
        auto dur = now - last_report;
        auto dur_s = std::chrono::duration_cast<std::chrono::duration<double>>(dur).count();
        auto fps = frame_count / dur_s;

        auto avg_frametime = total_frametime / frame_count;

        auto level = longest_frametime > 10ms && longest_frametime > (avg_frametime * 1.75) ? LogLevel::warn : LogLevel::trace;
        log(level, "{}: FPS {:7.1f} | Frametime {:6} / {:6} / {:6}",
            name, fps,
            duration_to_string(shortest_frametime),
            duration_to_string(total_frametime / frame_count),
            duration_to_string(longest_frametime));

        last_report = now;
        longest_frametime = {};
        shortest_frametime = std::chrono::steady_clock::duration::max();
        total_frametime = {};
        frame_count = 0;
    }
}

// -----------------------------------------------------------------------------

static
bool show_stats_for(Surface* surface)
{
    while (Subsurface* subsurface = Subsurface::from(surface)) surface = subsurface->parent();
    Toplevel* toplevel = Toplevel::from(surface);
    return toplevel && toplevel->report_stats;
}

void surface_profiler_report_commit(Surface* surface)
{
    if (show_stats_for(surface)) {
        surface->frame_commit_reporter.frame(std::format("{} commit  ", surface_to_string(surface)));
    }
}
