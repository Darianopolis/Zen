#include "quartz.hpp"

struct wlr_surface* qz_toplevel_get_surface(struct qz_toplevel* toplevel)
{
    if (toplevel->xdg_toplevel && toplevel->xdg_toplevel->base) {
        return toplevel->xdg_toplevel->base->surface;
    }

    return nullptr;
}

void qz_toplevel_set_bounds(struct qz_toplevel* toplevel, wlr_box box)
{
    // TODO: Does this need to be rate limited?
    //       if (toplevel->last_width != new_width || toplevel->last_height != new_height) {
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, box.width, box.height);
    wlr_scene_node_set_position(&toplevel->scene_tree->node, box.x, box.y);
}

wlr_box qz_toplevel_get_bounds(struct qz_toplevel* toplevel)
{
    wlr_box box;
    box.x = toplevel->scene_tree->node.x;
    box.y = toplevel->scene_tree->node.y;
    box.width = toplevel->xdg_toplevel->base->current.geometry.width;
    box.height = toplevel->xdg_toplevel->base->current.geometry.height;
    return box;
}

bool qz_toplevel_wants_fullscreen(struct qz_toplevel* toplevel)
{
    return toplevel->xdg_toplevel->requested.fullscreen;
}

void qz_toplevel_set_fullscreen(struct qz_toplevel* toplevel, bool fullscreen)
{
    wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, fullscreen);
}

void qz_focus_toplevel(struct qz_toplevel* toplevel)
{
    if (!toplevel) return;

    struct qz_server* server = toplevel->server;
    struct wlr_seat* seat = server->seat;
    struct wlr_surface* prev_surface = seat->keyboard_state.focused_surface;
    struct wlr_surface* surface = qz_toplevel_get_surface(toplevel);

    server->focused_toplevel = toplevel;

    if (prev_surface == surface) return;

    if (prev_surface) {
        struct wlr_xdg_toplevel* prev_toplevel = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel) {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }
    }

    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    wl_list_remove(&toplevel->link);
    wl_list_insert(&server->toplevels, &toplevel->link);
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
}

struct qz_toplevel* qz_desktop_toplevel_at(struct qz_server* server, double lx, double ly, struct wlr_surface** surface, double *sx, double *sy)
{
    struct wlr_scene_node* node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }

    struct wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface* scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return NULL;
    }

    *surface = scene_surface->surface;

    struct wlr_scene_tree* tree = node->parent;
    // TODO: This `tree != NULL` seems pointless. If it's ever triggered then the subsequent `tree->node.data` will always segfault
    while (tree != NULL && tree->node.data == NULL) {
        tree = tree->node.parent;
    }

    return static_cast<struct qz_toplevel*>(tree->node.data);
}

void qz_xdg_toplevel_map(struct wl_listener* listener, void*)
{
    auto toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

    qz_focus_toplevel(toplevel);
}

void qz_xdg_toplevel_unmap(struct wl_listener* listener, void*)
{
    auto toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    // Reset cursor mode if grabbed toplevel was unmapped
    if (toplevel == toplevel->server->grabbed_toplevel) {
        qz_reset_cursor_mode(toplevel->server);
    }

    wl_list_remove(&toplevel->link);
}

void qz_xdg_toplevel_commit(struct wl_listener* listener, void*)
{
    auto toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    if (toplevel->xdg_toplevel->base->initial_commit) {
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
    }
}

void qz_xdg_toplevel_destroy(struct wl_listener* listener, void*)
{
    auto toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    if (!toplevel->server) {
        wlr_log(WLR_ERROR, "qz_toplevel destroyed that didn't have server reference!");
    } else {
        if (toplevel->server->focused_toplevel == toplevel) {
            toplevel->server->focused_toplevel = nullptr;
        }
    }

    delete toplevel;
}

void qz_begin_interactive(struct qz_toplevel* toplevel, enum qz_cursor_mode mode, uint32_t edges)
{
    if (toplevel->xdg_toplevel->current.fullscreen) {
        return;
    }

    struct qz_server* server = toplevel->server;

    server->grabbed_toplevel = toplevel;
    server->cursor_mode = mode;

    if (mode == QZ_CURSOR_MOVE) {
        server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
        server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
    } else {
        struct wlr_box* geo_box = &toplevel->xdg_toplevel->base->geometry;

        double border_x = (toplevel->scene_tree->node.x + geo_box->x) + ((edges & WLR_EDGE_RIGHT ) ? geo_box->width  : 0);
        double border_y = (toplevel->scene_tree->node.y + geo_box->y) + ((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);

        server->grab_x = server->cursor->x - border_x;
        server->grab_y = server->cursor->y - border_y;

        server->grab_geobox = *geo_box;
        server->grab_geobox.x += toplevel->scene_tree->node.x;
        server->grab_geobox.y += toplevel->scene_tree->node.y;

        server->resize_edges = edges;
    }
}

void qz_xdg_toplevel_request_maximize(struct wl_listener* listener, void*)
{
    auto toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    // NOTE: We won't support maximization, but we have to send a configure event anyway

    if (toplevel->xdg_toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
    }
}

void qz_xdg_toplevel_request_fullscreen(struct wl_listener* listener, void*)
{
    auto toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    bool fs = qz_toplevel_wants_fullscreen(toplevel);
    if (fs) {
        auto prev = qz_toplevel_get_bounds(toplevel);
        auto output = qz_get_output_for_toplevel(toplevel);
        if (output) {
            auto b = qz_output_get_bounds(output);
            qz_toplevel_set_fullscreen(toplevel, true);
            qz_toplevel_set_bounds(toplevel, b);
            toplevel->prev_bounds = prev;
            // TODO: With layers implemented, move output background rect to fullscreen layer
            //       The xdg-protocol specifies:
            //
            //       If the fullscreened surface is not opaque, the compositor must make
            //        sure that other screen content not part of the same surface tree (made
            //        up of subsurfaces, popups or similarly coupled surfaces) are not
            //        visible below the fullscreened surface.
        }
    } else {
        qz_toplevel_set_fullscreen(toplevel, false);
        qz_toplevel_set_bounds(toplevel, toplevel->prev_bounds);
    }
}

void qz_server_new_xdg_toplevel(struct wl_listener* listener, void* data)
{
    auto server = qz_listener_userdata<qz_server*>(listener);
    struct wlr_xdg_toplevel* xdg_toplevel = static_cast<struct wlr_xdg_toplevel*>(data);

    struct qz_toplevel* toplevel = new qz_toplevel{};
    toplevel->server = server;
    toplevel->xdg_toplevel = xdg_toplevel;
    toplevel->scene_tree = wlr_scene_xdg_surface_create(&toplevel->server->scene->tree, xdg_toplevel->base);
    toplevel->scene_tree->node.data = toplevel;
    xdg_toplevel->base->data = toplevel->scene_tree;

    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.map,                toplevel, qz_xdg_toplevel_map);
    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.unmap,              toplevel, qz_xdg_toplevel_unmap);
    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.commit,             toplevel, qz_xdg_toplevel_commit);

    toplevel->listeners.listen(&xdg_toplevel->events.destroy, toplevel, qz_xdg_toplevel_destroy);

    toplevel->listeners.listen(&xdg_toplevel->events.request_maximize,   toplevel, qz_xdg_toplevel_request_maximize);
    toplevel->listeners.listen(&xdg_toplevel->events.request_fullscreen, toplevel, qz_xdg_toplevel_request_fullscreen);
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

void qz_xdg_popup_commit(struct wl_listener* listener, void*)
{
    auto popup = qz_listener_userdata<qz_popup*>(listener);

    if (popup->xdg_popup->base->initial_commit) {
        // When an xdg_surface performs an initial commit, the compositor must reply with a configure so the client can map the surface.
        // TODO: Ensure good popup geometry (e.g. centered, on screen)
    }
}

void qz_xdg_popup_destroy(struct wl_listener* listener, void*)
{
    auto popup = qz_listener_userdata<qz_popup*>(listener);

    delete popup;
}

void qz_server_new_xdg_popup(struct wl_listener*, void* data)
{
    struct wlr_xdg_popup* xdg_popup = static_cast<struct wlr_xdg_popup*>(data);

    struct qz_popup* popup = new qz_popup{};
    popup->xdg_popup = xdg_popup;

    struct wlr_xdg_surface* parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    assert(parent != nullptr);
    struct wlr_scene_tree* parent_tree = static_cast<struct wlr_scene_tree*>(parent->data);
    xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

    popup->listeners.listen(&xdg_popup->base->surface->events.commit,  popup,  qz_xdg_popup_commit);
    popup->listeners.listen(&               xdg_popup->events.destroy, popup, qz_xdg_popup_destroy);
}
