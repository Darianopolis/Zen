#include "quartz.hpp"

static
void qz_toplevel_update_border(qz_toplevel* toplevel)
{
    static constexpr uint32_t left = 0;
    static constexpr uint32_t top = 1;
    static constexpr uint32_t right = 2;
    static constexpr uint32_t bottom = 3;

    wlr_box bounds = qz_client_get_bounds(toplevel);

    bool show = bounds.width && bounds.height;
    show &= !toplevel->xdg_toplevel->current.fullscreen;

    wlr_box positions[4];
    positions[left]   = { -qz_border_width, -qz_border_width, qz_border_width, bounds.height + qz_border_width * 2 };
    positions[right]  = {  bounds.width,    -qz_border_width, qz_border_width, bounds.height + qz_border_width * 2 };
    positions[top]    = {  0,               -qz_border_width, bounds.width,    qz_border_width };
    positions[bottom] = {  0,                bounds.height,   bounds.width,    qz_border_width };

    for (uint32_t i = 0; i < 4; ++i) {
        if (show) {
            wlr_scene_node_set_enabled(&toplevel->border[i]->node, true);
            wlr_scene_node_set_position(&toplevel->border[i]->node, positions[i].x, positions[i].y);
            wlr_scene_rect_set_size(toplevel->border[i], positions[i].width, positions[i].height);
            wlr_scene_rect_set_color(toplevel->border[i],
                toplevel->server->focused_toplevel == toplevel
                    ? qz_border_color_focused.values
                    : qz_border_color_unfocused.values);
        } else {
            wlr_scene_node_set_enabled(&toplevel->border[i]->node, false);
        }
    }
}

wlr_surface* qz_toplevel_get_surface(qz_toplevel* toplevel)
{
    if (toplevel->xdg_toplevel && toplevel->xdg_toplevel->base) {
        return toplevel->xdg_toplevel->base->surface;
    }

    return nullptr;
}

wlr_box qz_client_get_geometry(qz_client* client)
{
    wlr_box geom = client->xdg_surface->current.geometry;

    // Some clients fail to report valid geometry. Fall back to surface dimensions.
    if (geom.width == 0)  geom.width  = client->xdg_surface->surface->current.width  - geom.x;
    if (geom.height == 0) geom.height = client->xdg_surface->surface->current.height - geom.y;

    return geom;
}

wlr_box qz_client_get_coord_system(qz_client* client)
{
    wlr_box box = {};
    wlr_scene_node_coords(&client->scene_tree->node, &box.x, &box.y);

    // Scene node coordinates position the geometry origin, adjust to surface origin
    box.x -= client->xdg_surface->current.geometry.x;
    box.y -= client->xdg_surface->current.geometry.y;

    box.width = client->xdg_surface->surface->current.width;
    box.height = client->xdg_surface->surface->current.height;

    return box;
}

wlr_box qz_client_get_bounds(qz_client* client)
{
    wlr_box box = qz_client_get_geometry(client);
    wlr_scene_node_coords(&client->scene_tree->node, &box.x, &box.y);
    return box;
}

void qz_toplevel_set_bounds(qz_toplevel* toplevel, wlr_box box)
{
    // NOTE: Bounds are set with parent node relative positions, unlike get_bounds which returns layout relative positions
    //       Thus you must be careful when setting/getting bounds with positioned parents
    // TODO: Tidy up this API and make it clear what is relative to what.
    //       Investigate Wayland's/wlroot's coordinate systems

    // TODO: Rate limit resize requests

    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, box.width, box.height);
    wlr_scene_node_set_position(&toplevel->scene_tree->node, box.x, box.y);
}

bool qz_toplevel_wants_fullscreen(qz_toplevel* toplevel)
{
    return toplevel->xdg_toplevel->requested.fullscreen;
}

void qz_toplevel_set_fullscreen(qz_toplevel* toplevel, bool fullscreen)
{
    if (fullscreen) {
        wlr_box prev = qz_client_get_bounds(toplevel);
        qz_output* output = qz_get_output_for_client(toplevel);
        if (output) {
            wlr_box b = qz_output_get_bounds(output);
            wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, true);
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
        wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, false);
        qz_toplevel_set_bounds(toplevel, toplevel->prev_bounds);
    }
}

void qz_toplevel_set_activated(qz_toplevel* toplevel, bool active)
{
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, active);
    qz_toplevel_update_border(toplevel);
}

void qz_cycle_focus_immediate(qz_server* server, wlr_cursor* cursor, bool backwards)
{
    if (backwards) {
        for (qz_toplevel* toplevel : server->toplevels) {
            if (toplevel == server->focused_toplevel) continue;
            if (cursor) {
                wlr_box bounds = qz_client_get_bounds(toplevel);
                if (!wlr_box_contains_point(&bounds, cursor->x, cursor->y)) continue;
            }

            qz_toplevel_focus(toplevel);
            return;
        }
    } else {
        uint32_t i = server->toplevels.size();
        while (i-- > 0) {
            qz_toplevel* toplevel = server->toplevels[i];
            if (toplevel == server->focused_toplevel) continue;
            if (cursor) {
                wlr_box bounds = qz_client_get_bounds(toplevel);
                if (!wlr_box_contains_point(&bounds, cursor->x, cursor->y)) continue;
            }

            if (server->focused_toplevel) {
                // Re-insert currently focused window at bottom of focus stack
                std::erase(server->toplevels, server->focused_toplevel);
                server->toplevels.insert(server->toplevels.begin(), server->focused_toplevel);
            }

            // Focus new window
            qz_toplevel_focus(toplevel);
            return;
        }
    }
}

void qz_toplevel_focus(qz_toplevel* toplevel)
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
            qz_toplevel_set_activated(static_cast<qz_toplevel*>(prev_toplevel->base->data), false);
        }
    }

    wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    std::erase(server->toplevels, toplevel);
    server->toplevels.emplace_back(toplevel);
    qz_toplevel_set_activated(toplevel, true);

    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }

    qz_toplevel_update_border(toplevel);
}

void qz_toplevel_unfocus(qz_toplevel* toplevel)
{
    if (!toplevel) return;
    if (toplevel->server->focused_toplevel != toplevel)
        return;

    toplevel->server->focused_toplevel = nullptr;
    wlr_seat_keyboard_notify_clear_focus(toplevel->server->seat);

    qz_toplevel_update_border(toplevel);
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

    toplevel->server->toplevels.emplace_back(toplevel);

    qz_toplevel_focus(toplevel);
}

void qz_toplevel_unmap(wl_listener* listener, void*)
{
    qz_toplevel* toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    // Reset cursor mode if grabbed toplevel was unmapped
    if (toplevel == toplevel->server->grabbed_toplevel) {
        qz_reset_cursor_mode(toplevel->server);
    }

    // TODO: Handle toplevel unmap during zone operation

    qz_toplevel_unfocus(toplevel);
    std::erase(toplevel->server->toplevels, toplevel);
    if (!toplevel->server->toplevels.empty()) {
        qz_toplevel_focus(toplevel->server->toplevels.back());
    }
}

void qz_toplevel_commit(wl_listener* listener, void*)
{
    qz_toplevel* toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    if (toplevel->xdg_toplevel->base->initial_commit) {
        qz_decoration_set_mode(toplevel);
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
    }

    if (!toplevel->xdg_toplevel->current.width || !toplevel->xdg_toplevel->current.height) {
        // Update initial toplevel size to reflect commited geometry
        wlr_box geom = qz_client_get_geometry(toplevel);
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, geom.width, geom.height);
    }

    qz_toplevel_update_border(toplevel);
}

void qz_toplevel_destroy(wl_listener* listener, void*)
{
    qz_toplevel* toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    delete toplevel;
}

bool qz_toplevel_is_interactable(qz_toplevel* toplevel)
{
    if (toplevel->xdg_toplevel->current.fullscreen) return false;

    return true;
}

void qz_toplevel_begin_interactive(qz_toplevel* toplevel, qz_cursor_mode mode, uint32_t edges)
{
    if (!qz_toplevel_is_interactable(toplevel)) return;

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

    qz_toplevel_set_fullscreen(toplevel, qz_toplevel_wants_fullscreen(toplevel));
}

void qz_server_new_toplevel(wl_listener* listener, void* data)
{
    qz_server* server = qz_listener_userdata<qz_server*>(listener);
    wlr_xdg_toplevel* xdg_toplevel = static_cast<wlr_xdg_toplevel*>(data);

    qz_toplevel* toplevel = new qz_toplevel{};
    toplevel->type = qz_client_type::toplevel;
    toplevel->server = server;
    toplevel->xdg_surface = xdg_toplevel->base;
    toplevel->xdg_surface->data = toplevel;

    toplevel->xdg_toplevel = xdg_toplevel;
    toplevel->scene_tree = wlr_scene_xdg_surface_create(&toplevel->server->scene->tree, xdg_toplevel->base);
    toplevel->scene_tree->node.data = toplevel;

    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.map,    toplevel, qz_toplevel_map);
    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.unmap,  toplevel, qz_toplevel_unmap);
    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.commit, toplevel, qz_toplevel_commit);

    toplevel->listeners.listen(&xdg_toplevel->events.destroy, toplevel, qz_toplevel_destroy);

    toplevel->listeners.listen(&xdg_toplevel->events.request_maximize,   toplevel, qz_toplevel_request_maximize);
    toplevel->listeners.listen(&xdg_toplevel->events.request_fullscreen, toplevel, qz_toplevel_request_fullscreen);

    for (int i = 0; i < 4; ++i) {
        toplevel->border[i] = wlr_scene_rect_create(toplevel->scene_tree, 0, 0, qz_border_color_unfocused.values);
    }
}

// -----------------------------------------------------------------------------

void qz_decoration_set_mode(qz_toplevel* toplevel)
{
    if (!toplevel->decoration.xdg_decoration) return;

    if (toplevel->xdg_toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration.xdg_decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
}

void qz_decoration_new(wl_listener*, void* data)
{
    wlr_xdg_toplevel_decoration_v1* xdg_decoration = static_cast<wlr_xdg_toplevel_decoration_v1*>(data);

    qz_toplevel* toplevel = static_cast<qz_toplevel*>(xdg_decoration->toplevel->base->data);
    if (toplevel->decoration.xdg_decoration) {
        wlr_log(WLR_ERROR, "Toplevel already has attached decoration!");
        return;
    }

    toplevel->decoration.xdg_decoration = xdg_decoration;

    toplevel->decoration.listeners.listen(&xdg_decoration->events.request_mode, toplevel, qz_decoration_request_mode);
    toplevel->decoration.listeners.listen(&xdg_decoration->events.destroy,      toplevel, qz_decoration_destroy);

    qz_decoration_set_mode(toplevel);
}

void qz_decoration_request_mode(wl_listener* listener, void*)
{
    qz_toplevel* toplevel = qz_listener_userdata<qz_toplevel*>(listener);
    qz_decoration_set_mode(toplevel);
}

void qz_decoration_destroy(wl_listener* listener, void*)
{
    qz_toplevel* toplevel = qz_listener_userdata<qz_toplevel*>(listener);

    toplevel->decoration.xdg_decoration = nullptr;
    toplevel->decoration.listeners.clear();
}

// -----------------------------------------------------------------------------

void qz_popup_commit(wl_listener* listener, void*)
{
    qz_popup* popup = qz_listener_userdata<qz_popup*>(listener);

    wlr_xdg_popup* xdg_popup = popup->xdg_popup;

    if (!xdg_popup->base->initial_commit) {
        return;
    }

    wlr_xdg_surface* xdg_parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    qz_client* parent = static_cast<qz_client*>(xdg_parent->data);
    popup->scene_tree = wlr_scene_xdg_surface_create(parent->scene_tree, xdg_popup->base);

    qz_output* output = qz_get_output_for_client(parent);
    if (output) {
        wlr_box output_bounds = qz_output_get_bounds(output);

        {
            // TODO: Move this into a helper for getting a qz_toplevel from an wlr_(xdg_)surface
            qz_client* cur = parent;
            while (cur->type == qz_client_type::popup) {
                cur = static_cast<qz_client*>(wlr_xdg_surface_try_from_wlr_surface(static_cast<qz_popup*>(cur)->xdg_popup->parent)->data);
            }

            // Adjust output bounds to be in the root toplevel's surface coordinate system.
            wlr_box coord_system = qz_client_get_coord_system(cur);
            output_bounds.x -= coord_system.x;
            output_bounds.y -= coord_system.y;
        }

        wlr_xdg_popup_unconstrain_from_box(xdg_popup, &output_bounds);
    } else {
        wlr_log(WLR_ERROR, "No output for toplevel while opening popup!");
    }
}

void qz_popup_destroy(wl_listener* listener, void*)
{
    qz_popup* popup = qz_listener_userdata<qz_popup*>(listener);

    delete popup;
}

void qz_server_new_popup(wl_listener* listener, void* data)
{
    qz_server* server = qz_listener_userdata<qz_server*>(listener);
    wlr_xdg_popup* xdg_popup = static_cast<wlr_xdg_popup*>(data);

    qz_popup* popup = new qz_popup{};
    popup->type = qz_client_type::popup;
    popup->server = server;
    popup->xdg_surface = xdg_popup->base;
    popup->xdg_surface->data = popup;

    popup->xdg_popup = xdg_popup;

    popup->listeners.listen(&xdg_popup->base->surface->events.commit,  popup, qz_popup_commit);
    popup->listeners.listen(&               xdg_popup->events.destroy, popup, qz_popup_destroy);
}
