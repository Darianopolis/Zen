#include "core.hpp"

std::string to_string(Surface* surface)
{
    return surface ? surface->to_string() : "nullptr";
}

std::string Toplevel::to_string()
{
    return std::format("XdgToplevel<{}>({}, {})",
        (void*)this,
        xdg_toplevel->app_id ? xdg_toplevel->app_id : "?",
        xdg_toplevel->title ? xdg_toplevel->title   : "?");
}

std::string Popup::to_string() { return std::format("XdgPopup<{}>", (void*)this); }
std::string LayerSurface::to_string() { return std::format("WlrLayerSurface<{}>", (void*)this); }
std::string CursorSurface::to_string()
{
    return std::format("CursorSurface<{}>(wlr_surface = {}, visisble = {})", (void*)this, (void*)surface, cursor_surface_is_visible(this));
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

    return std::format("CursorSurface<{}>(wlr_surface = {}, visisble = {})", (void*)cursor_surface, (void*)cursor_surface->wlr_surface(), cursor_surface_is_visible(cursor_surface));
}

std::string pointer_to_string(Pointer* pointer)
{
    return pointer ? std::format("Pointer<{}>", (void*)pointer) : "nullptr";
}