#include "core.hpp"

#include <xcb/xproto.h>

void xwayland_init(Server* server)
{
    server->xwayland = wlr_xwayland_create(server->display, server->compositor, true);
    if (!server->xwayland) {
        unsetenv("DISPLAY");
        log_error("Failed to create XWayland server");
    }

    server->listeners.listen(&server->xwayland->events.ready, server, xwayland_ready);
    server->listeners.listen(&server->xwayland->events.new_surface, server, xwayland_new);

    setenv("DISPLAY", server->xwayland->display_name, 1);
}

void xwayland_cleanup(Server* server)
{
    wlr_xwayland_destroy(server->xwayland);
}

void xwayland_ready(wl_listener* listener, void*)
{
    Server* server = listener_userdata<Server*>(listener);

    log_warn("XWayland ready!");

    wlr_xwayland_set_seat(server->xwayland, server->seat);

    wlr_xcursor* xcursor = wlr_xcursor_manager_get_xcursor(server->cursor_manager, "default", 1);
    if (xcursor) {
        wlr_xcursor_image* i = xcursor->images[0];
        wlr_xwayland_set_cursor(server->xwayland, i->buffer, i->width * 4, i->width, i->height, i->hotspot_x, i->hotspot_y);
    }
}


void xwayland_destroy(wl_listener* listener, void*)
{
    XWaylandSurface* surface = listener_userdata<XWaylandSurface*>(listener);

    log_debug("XWayland surface destroyed: {}", surface_to_string(surface));

    surface_cleanup(surface);

    delete surface;
}

void xwayland_new(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_xwayland_surface* xwayland_surface = static_cast<wlr_xwayland_surface*>(data);

    XWaylandSurface* surface = new XWaylandSurface{};
    surface->role = xwayland_surface->override_redirect ? SurfaceRole::popup : SurfaceRole::toplevel;
    surface->server = server;
    surface->xwayland_surface = xwayland_surface;

    log_debug("XWayland surface created: {}", surface_to_string(surface));

    surface->popup_tree = wlr_scene_tree_create(server->layers[Strata::top]);

    surface->listeners.listen(&xwayland_surface->events.destroy,            surface, xwayland_destroy);
    surface->listeners.listen(&xwayland_surface->events.associate,          surface, xwayland_associate);
    surface->listeners.listen(&xwayland_surface->events.dissociate,         surface, xwayland_dissociate);
    surface->listeners.listen(&xwayland_surface->events.request_activate,   surface, xwayland_activate);
    surface->listeners.listen(&xwayland_surface->events.request_configure,  surface, xwayland_configure);
    surface->listeners.listen(&xwayland_surface->events.request_fullscreen, surface, xwayland_request_fullscreen);
}

void xwayland_associate(wl_listener* listener, void*)
{
    XWaylandSurface* surface = listener_userdata<XWaylandSurface*>(listener);

    log_warn("XWayland associate (wlr_surface = {}) for {}", (void*)surface->xwayland_surface->surface, surface_to_string(surface));

    surface->wlr_surface = surface->xwayland_surface->surface;
    surface->wlr_surface->data = surface;

    surface->scene_tree = wlr_scene_subsurface_tree_create(surface->server->layers[Strata::floating], surface->wlr_surface);
    surface->scene_tree->node.data = surface;

    if (surface->role == SurfaceRole::toplevel) {
        for (int i = 0; i < 4; ++i) {
            surface->borders[i] = wlr_scene_rect_create(surface->scene_tree, 0, 0, border_color_unfocused.values);
        }
    }

    surface->map_listeners.listen(&surface->wlr_surface->events.map, static_cast<Surface*>(surface),   surface_map);
    surface->map_listeners.listen(&surface->wlr_surface->events.unmap, static_cast<Surface*>(surface), surface_unmap);
}

void xwayland_dissociate(wl_listener* listener, void*)
{
    XWaylandSurface* surface = listener_userdata<XWaylandSurface*>(listener);

    log_warn("XWayland dissociate (wlr_surface = {}) for {}", (void*)surface->wlr_surface, surface_to_string(surface));

    surface->map_listeners.clear();

    if (surface->wlr_surface) {
        surface->wlr_surface->data = nullptr;
        surface->wlr_surface = nullptr;
    }

    if (surface->scene_tree) {
        log_warn("destroying node", (void*)&surface->scene_tree->node);
        wlr_scene_node_reparent(&surface->scene_tree->node, &surface->server->scene->tree);
        for (auto* node = &surface->scene_tree->node; node; node = &node->parent->node) {
            log_warn("  parent: {}", (void*)node);
        }
        wlr_scene_node_destroy(&surface->scene_tree->node);
        surface->scene_tree = nullptr;
    }
}

void xwayland_activate(wl_listener* listener, void*)
{
    log_warn("XWayland activate");

    XWaylandSurface* toplevel = listener_userdata<XWaylandSurface*>(listener);

    // TODO: Check if window is "managed"
    if (toplevel->role == SurfaceRole::toplevel) {
        wlr_xwayland_surface_activate(toplevel->xwayland_surface, true);
    }
}

void xwayland_configure(wl_listener* listener, void* data)
{
    log_warn("XWayland configure");

    XWaylandSurface* surface = listener_userdata<XWaylandSurface*>(listener);
    wlr_xwayland_surface_configure_event* event = static_cast<wlr_xwayland_surface_configure_event*>(data);

    log_warn("  bounds = ({}, {}) ({}, {})", event->x, event->y, event->width, event->height);

    if (!surface->wlr_surface || !surface->wlr_surface->mapped) {
        wlr_xwayland_surface_configure(surface->xwayland_surface, event->x, event->y, event->width, event->height);
        return;
    }

    toplevel_set_bounds(surface, wlr_box { .x = event->x, .y = event->y, .width = event->width, .height = event->height });
    toplevel_update_border(surface);
}

void xwayland_request_fullscreen(wl_listener* listener, void*)
{
    XWaylandSurface* surface = listener_userdata<XWaylandSurface*>(listener);

    log_warn("XWayland client request fullscreen = {} from {}", surface->xwayland_surface->fullscreen, surface_to_string(surface));
}