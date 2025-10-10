#include "quartz.hpp"

#ifdef QZ_XWAYLAND

#define QZ_XTRACE_ENTER wlr_log(WLR_INFO, ">>>> %s", __func__);
#define QZ_XTRACE_LEAVE wlr_log(WLR_INFO, "<<<< %s", __func__);

void qz_init_xwayland(struct qz_server* server)
{
    QZ_XTRACE_ENTER

    if ((server->xwayland = wlr_xwayland_create(server->wl_display, server->compositor, 1))) {
        QZ_LISTEN(server->xwayland->events.ready,       server->xwayland_ready,       qz_xwayland_ready);
        QZ_LISTEN(server->xwayland->events.new_surface, server->new_xwayland_surface, qz_new_xwayland_surface);

        setenv("DISPLAY", server->xwayland->display_name, 1);
    } else {
        wlr_log(WLR_ERROR, "Failed to set up XWayland X server, continuing without it\n");
    }

    QZ_XTRACE_LEAVE
}

void qz_destroy_xwayland(struct qz_server* server)
{
    QZ_XTRACE_ENTER

    QZ_UNLISTEN(server->new_xwayland_surface);
    QZ_UNLISTEN(server->xwayland_ready);

    wlr_xwayland_destroy(server->xwayland);
    server->xwayland = nullptr;

    QZ_XTRACE_LEAVE
}

void qz_xwayland_ready(struct wl_listener* listener, void*)
{
    QZ_XTRACE_ENTER

    struct qz_server* server = wl_container_of(listener, server, xwayland_ready);
    struct wlr_xcursor* xcursor;

    wlr_xwayland_set_seat(server->xwayland, server->seat);

    if ((xcursor = wlr_xcursor_manager_get_xcursor(server->cursor_manager, "default", 1))) {
        wlr_xwayland_set_cursor(server->xwayland,
            xcursor->images[0]->buffer, xcursor->images[0]->width * 4,
            xcursor->images[0]->width, xcursor->images[0]->height,
            xcursor->images[0]->hotspot_x, xcursor->images[0]->hotspot_y);
    }

    QZ_XTRACE_LEAVE
}

void qz_xwayland_associate(struct wl_listener* listener, void*)
{
    QZ_XTRACE_ENTER

    struct qz_toplevel* toplevel = wl_container_of(listener, toplevel, x_associate);

    QZ_LISTEN(qz_toplevel_get_surface(toplevel)->events.map,   toplevel->map,   qz_xdg_toplevel_map);
    QZ_LISTEN(qz_toplevel_get_surface(toplevel)->events.unmap, toplevel->unmap, qz_xdg_toplevel_unmap);

    QZ_XTRACE_LEAVE
}

void qz_xwayland_dissociate(struct wl_listener* listener, void*)
{
    QZ_XTRACE_ENTER

    struct qz_toplevel* toplevel = wl_container_of(listener, toplevel, x_dissociate);

    QZ_UNLISTEN(toplevel->map);
    QZ_UNLISTEN(toplevel->unmap);

    QZ_XTRACE_LEAVE
}

void qz_xwayland_request_activate(struct wl_listener* listener, void*)
{
    QZ_XTRACE_ENTER

    struct qz_toplevel* toplevel = wl_container_of(listener, toplevel, x_activate);

    if (!qz_toplevel_is_unmanaged(toplevel)) {
        wlr_xwayland_surface_activate(toplevel->xwayland_surface, 1);
    }

    QZ_XTRACE_LEAVE
}

void qz_xwayland_request_configure(struct wl_listener* listener, void* data)
{
    QZ_XTRACE_ENTER

    struct qz_toplevel* toplevel = wl_container_of(listener, toplevel, x_configure);
    struct wlr_xwayland_surface_configure_event* event = static_cast<struct wlr_xwayland_surface_configure_event*>(data);

    if (!toplevel->xwayland_surface->surface || !toplevel->xwayland_surface->surface->mapped) {
        wlr_xwayland_surface_configure(toplevel->xwayland_surface, event->x, event->y, event->width, event->height);

        QZ_XTRACE_LEAVE
        return;
    }

    // TODO: Handle border sizing for "managed" windows
    // TODO: Handle layout snapping

    // if (qz_toplevel_is_unmanaged(toplevel)) {

    wlr_scene_node_set_position(&toplevel->scene_tree->node, event->x, event->y);
    wlr_xwayland_surface_configure(toplevel->xwayland_surface, event->x, event->y, event->width, event->height);

    // }

    QZ_XTRACE_LEAVE
}

void qz_new_xwayland_surface(struct wl_listener* listener, void* data)
{
    QZ_XTRACE_ENTER

    struct qz_server* server = wl_container_of(listener, server, new_xwayland_surface);
    struct wlr_xwayland_surface* surface = static_cast<struct wlr_xwayland_surface*>(data);

    struct qz_toplevel* toplevel;

    toplevel = static_cast<struct qz_toplevel*>(calloc(1, sizeof(*toplevel)));
    toplevel->server = server;
    toplevel->xwayland_surface = surface;
    surface->data = toplevel;

    struct wlr_scene_tree* scene = wlr_scene_tree_create(&server->scene->tree);
    toplevel->scene_tree = wlr_scene_subsurface_tree_create(scene, surface->surface);
    toplevel->scene_tree->node.data = toplevel;

    QZ_LISTEN(surface->events.associate,          toplevel->x_associate,        qz_xwayland_associate);
    QZ_LISTEN(surface->events.dissociate,         toplevel->x_dissociate,       qz_xwayland_dissociate);
    QZ_LISTEN(surface->events.request_activate,   toplevel->x_activate,         qz_xwayland_request_activate);
    QZ_LISTEN(surface->events.request_configure,  toplevel->x_configure,        qz_xwayland_request_configure);

    QZ_LISTEN(surface->events.destroy,            toplevel->destroy,            qz_xdg_toplevel_destroy);
    QZ_LISTEN(surface->events.request_fullscreen, toplevel->request_fullscreen, qz_xdg_toplevel_request_fullscreen);

    QZ_XTRACE_LEAVE
}

#endif
