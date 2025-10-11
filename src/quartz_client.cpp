#include "quartz.hpp"

wlr_surface* qz_toplevel_get_surface(qz_toplevel* toplevel)
{
    if (toplevel->xdg_toplevel && toplevel->xdg_toplevel->base) {
        return toplevel->xdg_toplevel->base->surface;
    }

    return nullptr;
}

void qz_toplevel_set_bounds(qz_toplevel* toplevel, wlr_box box)
{
    // TODO: Does this need to be rate limited?
    //       if (toplevel->last_width != new_width || toplevel->last_height != new_height) {
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, box.width, box.height);
    wlr_scene_node_set_position(&toplevel->scene_tree->node, box.x, box.y);
}

wlr_box qz_toplevel_get_bounds(qz_toplevel* toplevel)
{
    wlr_box box;
    box.x = toplevel->scene_tree->node.x;
    box.y = toplevel->scene_tree->node.y;
    box.width = toplevel->xdg_toplevel->base->current.geometry.width;
    box.height = toplevel->xdg_toplevel->base->current.geometry.height;
    return box;
}

bool qz_toplevel_wants_fullscreen(qz_toplevel* toplevel)
{
    return toplevel->xdg_toplevel->requested.fullscreen;
}

void qz_toplevel_set_fullscreen(qz_toplevel* toplevel, bool fullscreen)
{
    wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, fullscreen);
}

void qz_focus_toplevel(qz_toplevel* toplevel)
{
    // NOTE: This function only deals with keyboard focus

    if (!toplevel) return;

    qz_server* server = toplevel->server;
    wlr_seat* seat = server->seat;
    wlr_surface* prev_surface = seat->keyboard_state.focused_surface;
    wlr_surface* surface = qz_toplevel_get_surface(toplevel);

    server->focused_toplevel = toplevel;

    if (prev_surface == surface) return;

    if (prev_surface) {
        wlr_xdg_toplevel* prev_toplevel = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel) {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }
    }

    wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    wl_list_remove(&toplevel->link);
    wl_list_insert(&server->toplevels, &toplevel->link);
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
}

qz_toplevel* qz_get_toplevel_at(qz_server* server, double lx, double ly, wlr_surface** surface, double *sx, double *sy)
{
    // This returns the topmost node in the scene at the given layout coords.
    // We only care about surface nodes as we are specifically looking for a surface in the surface tree of a quartz_client

    wlr_scene_node* node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }

    wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
    wlr_scene_surface* scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return NULL;
    }

    *surface = scene_surface->surface;

    wlr_scene_tree* tree = node->parent;
    // TODO: This `tree != NULL` seems pointless. If it's ever triggered then the subsequent `tree->node.data` will always segfault
    while (tree != NULL && tree->node.data == NULL) {
        tree = tree->node.parent;
    }

    return static_cast<qz_toplevel*>(tree->node.data);
}

void qz_toplevel_map(wl_listener* listener, void*)
{
    qz_toplevel* toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

    qz_focus_toplevel(toplevel);
}

void qz_toplevel_unmap(wl_listener* listener, void*)
{
    qz_toplevel* toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    // Reset cursor mode if grabbed toplevel was unmapped
    if (toplevel == toplevel->server->grabbed_toplevel) {
        qz_reset_cursor_mode(toplevel->server);
    }

    wl_list_remove(&toplevel->link);
}

void qz_toplevel_commit(wl_listener* listener, void*)
{
    qz_toplevel* toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    if (toplevel->xdg_toplevel->base->initial_commit) {
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
    }
}

void qz_toplevel_destroy(wl_listener* listener, void*)
{
    qz_toplevel* toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    if (!toplevel->server) {
        wlr_log(WLR_ERROR, "qz_toplevel destroyed that didn't have server reference!");
    } else {
        if (toplevel->server->focused_toplevel == toplevel) {
            toplevel->server->focused_toplevel = nullptr;
        }
    }

    delete toplevel;
}

void qz_toplevel_begin_interactive(qz_toplevel* toplevel, qz_cursor_mode mode, uint32_t edges)
{
    if (toplevel->xdg_toplevel->current.fullscreen) {
        return;
    }

    qz_server* server = toplevel->server;

    server->grabbed_toplevel = toplevel;
    server->cursor_mode = mode;

    if (mode == qz_cursor_mode::move) {
        server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
        server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
    } else {
        wlr_box* geo_box = &toplevel->xdg_toplevel->base->geometry;

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

void qz_toplevel_request_maximize(wl_listener* listener, void*)
{
    qz_toplevel* toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    // NOTE: We won't support maximization, but we have to send a configure event anyway

    if (toplevel->xdg_toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
    }
}

void qz_toplevel_request_fullscreen(wl_listener* listener, void*)
{
    qz_toplevel* toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    bool fs = qz_toplevel_wants_fullscreen(toplevel);
    if (fs) {
        wlr_box prev = qz_toplevel_get_bounds(toplevel);
        qz_output* output = qz_get_output_for_toplevel(toplevel);
        if (output) {
            wlr_box b = qz_output_get_bounds(output);
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

void qz_server_new_xdg_toplevel(wl_listener* listener, void* data)
{
    qz_server* server = qz_listener_userdata<qz_server*>(listener);
    wlr_xdg_toplevel* xdg_toplevel = static_cast<wlr_xdg_toplevel*>(data);

    qz_toplevel* toplevel = new qz_toplevel{};
    toplevel->server = server;
    toplevel->xdg_toplevel = xdg_toplevel;
    toplevel->scene_tree = wlr_scene_xdg_surface_create(&toplevel->server->scene->tree, xdg_toplevel->base);
    toplevel->scene_tree->node.data = toplevel;
    xdg_toplevel->base->data = toplevel->scene_tree;

    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.map,    toplevel, qz_toplevel_map);
    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.unmap,  toplevel, qz_toplevel_unmap);
    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.commit, toplevel, qz_toplevel_commit);

    toplevel->listeners.listen(&xdg_toplevel->events.destroy, toplevel, qz_toplevel_destroy);

    toplevel->listeners.listen(&xdg_toplevel->events.request_maximize,   toplevel, qz_toplevel_request_maximize);
    toplevel->listeners.listen(&xdg_toplevel->events.request_fullscreen, toplevel, qz_toplevel_request_fullscreen);
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

void qz_xdg_popup_commit(wl_listener* listener, void*)
{
    qz_popup* popup = qz_listener_userdata<qz_popup*>(listener);

    if (popup->xdg_popup->base->initial_commit) {
        // When an xdg_surface performs an initial commit, the compositor must reply with a configure so the client can map the surface.
        // TODO: Ensure good popup geometry (e.g. centered, on screen)
    }
}

void qz_xdg_popup_destroy(wl_listener* listener, void*)
{
    qz_popup* popup = qz_listener_userdata<qz_popup*>(listener);

    delete popup;
}

void qz_server_new_xdg_popup(wl_listener*, void* data)
{
    wlr_xdg_popup* xdg_popup = static_cast<wlr_xdg_popup*>(data);

    qz_popup* popup = new qz_popup{};
    popup->xdg_popup = xdg_popup;

    wlr_xdg_surface* parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    assert(parent != nullptr);
    wlr_scene_tree* parent_tree = static_cast<wlr_scene_tree*>(parent->data);
    xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

    popup->listeners.listen(&xdg_popup->base->surface->events.commit,  popup,  qz_xdg_popup_commit);
    popup->listeners.listen(&               xdg_popup->events.destroy, popup, qz_xdg_popup_destroy);
}
