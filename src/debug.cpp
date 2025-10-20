#include "core.hpp"

std::string toplevel_to_string(Toplevel* toplevel)
{
    return toplevel
        ? std::format("Toplevel<{}>({}, {})",
            (void*)toplevel,
            toplevel->xdg_toplevel()->app_id ? toplevel->xdg_toplevel()->app_id : "?",
            toplevel->xdg_toplevel()->title ? toplevel->xdg_toplevel()->title   : "?")
        : "nullptr";
}

std::string surface_to_string(Surface* surface)
{
    if (!surface) return "nullptr";
    switch (surface->role) {
        case SurfaceRole::toplevel:      return toplevel_to_string(Toplevel::from(surface));
        case SurfaceRole::popup:         return std::format("Popup<{}>", (void*)surface);
        case SurfaceRole::layer_surface: return std::format("LayerSurface<{}>", (void*)surface);
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

std::string client_to_string(wl_client* client)
{
    if (!client) return "nullptr";

    pid_t pid;
    uid_t uid;
    gid_t gid;
    wl_client_get_credentials(client, &pid, &uid, &gid);

    std::ifstream file{std::format("/proc/{}/comm", pid), std::ios::binary};

    std::string name;
    std::getline(file, name);

    return std::format("Client<{}>(pid = {}, name = {})", (void*)client, pid, name);
}

std::string cursor_surface_to_string(CursorSurface* cursor_surface)
{
    if (!cursor_surface) return "nullptr";

    return std::format("CursorSurface<{}>(wlr_surface = {}, visisble = {})", (void*)cursor_surface, (void*)cursor_surface->wlr_surface, cursor_surface_is_visible(cursor_surface));
}
