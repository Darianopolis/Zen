#include "core.hpp"

static
std::string xdg_toplevel_to_string(XdgToplevel* toplevel)
{
    return std::format("XdgToplevel<{}>({}, {})",
            (void*)toplevel,
            toplevel->xdg_toplevel()->app_id ? toplevel->xdg_toplevel()->app_id : "?",
            toplevel->xdg_toplevel()->title ? toplevel->xdg_toplevel()->title   : "?");
}

static
std::string xsurface_to_string(XWaylandSurface* xwayland_surface)
{
    return std::format("XWaylandSurface<{}>({}, role = {})",
            (void*)xwayland_surface,
            xwayland_surface->xwayland_surface->title ? xwayland_surface->xwayland_surface->title : "?",
            magic_enum::enum_name(xwayland_surface->role));
}

std::string surface_to_string(Surface* surface)
{
    if (!surface) return "nullptr";

    if (XdgToplevel* xdg_toplevel = XdgToplevel::get_impl(surface))
        return xdg_toplevel_to_string(xdg_toplevel);
    if (XWaylandSurface* xwayland_surface = XWaylandSurface::get_impl(surface))
        return xsurface_to_string(xwayland_surface);
    if (XdgPopup* xdg_popup = XdgPopup::get_impl(surface))
        return std::format("XdgPopup<{}>", (void*)xdg_popup);
    if (LayerSurface* layer_surface = LayerSurface::get_impl(surface))
        return std::format("LayerSurface<{}>", (void*)layer_surface);

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

std::string pointer_to_string(Pointer* pointer)
{
    return pointer ? std::format("Pointer<{}>", (void*)pointer) : "nullptr";
}