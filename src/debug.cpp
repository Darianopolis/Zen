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

std::string layer_surface_to_string(LayerSurface* layer_surface)
{
    return std::format("LayerSurface<{}>(namespace = {}, interactivity = {})",
        (void*)layer_surface,
        layer_surface->wlr_layer_surface()->namespace_,
        magic_enum::enum_name(layer_surface->wlr_layer_surface()->current.keyboard_interactive)
            .substr(sizeof("ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_") - 1));
}

std::string surface_to_string(Surface* surface)
{
    if (!surface) return "nullptr";
    switch (surface->role) {
        case SurfaceRole::toplevel:      return toplevel_to_string(Toplevel::from(surface));
        case SurfaceRole::popup:         return std::format("Popup<{}>", (void*)surface);
        case SurfaceRole::layer_surface: return layer_surface_to_string(LayerSurface::from(surface));
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

    wlr_output_layout_output* layout_output = output->layout_output();

    return std::format("Output<{}>(name = {}, desc = {}, pos = ({}, {}), size = ({}, {}), refresh = {:.2f}Hz)",
        (void*)output,
        output->wlr_output->name,
        output->wlr_output->description,
        layout_output ? layout_output->x : -1,
        layout_output ? layout_output->y : -1,
        output->wlr_output->width,
        output->wlr_output->height,
        output->wlr_output->refresh / 1000.0
        );
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
