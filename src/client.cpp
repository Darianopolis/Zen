#include "core.hpp"

static
void toplevel_update_border(Toplevel* toplevel)
{
    static constexpr uint32_t left = 0;
    static constexpr uint32_t top = 1;
    static constexpr uint32_t right = 2;
    static constexpr uint32_t bottom = 3;

    wlr_box bounds = client_get_bounds(toplevel);

    bool show = bounds.width && bounds.height;
    show &= !toplevel->xdg_toplevel->current.fullscreen;

    wlr_box positions[4];
    positions[left]   = { -border_width, -border_width,  border_width, bounds.height + border_width * 2 };
    positions[right]  = {  bounds.width, -border_width,  border_width, bounds.height + border_width * 2 };
    positions[top]    = {  0,            -border_width,  bounds.width, border_width                     };
    positions[bottom] = {  0,             bounds.height, bounds.width, border_width                     };

    for (uint32_t i = 0; i < 4; ++i) {
        if (show) {
            wlr_scene_node_set_enabled(&toplevel->border[i]->node, true);
            wlr_scene_node_set_position(&toplevel->border[i]->node, positions[i].x, positions[i].y);
            wlr_scene_rect_set_size(toplevel->border[i], positions[i].width, positions[i].height);
            wlr_scene_rect_set_color(toplevel->border[i],
                toplevel->server->focused_toplevel == toplevel
                    ? border_color_focused.values
                    : border_color_unfocused.values);
        } else {
            wlr_scene_node_set_enabled(&toplevel->border[i]->node, false);
        }
    }
}

wlr_surface* toplevel_get_surface(Toplevel* toplevel)
{
    if (toplevel->xdg_toplevel && toplevel->xdg_toplevel->base) {
        return toplevel->xdg_toplevel->base->surface;
    }

    return nullptr;
}

wlr_box client_get_geometry(Client* client)
{
    wlr_box geom = client->xdg_surface->current.geometry;

    // Some clients (E.g. SDL3 + Vulkan) fail to report valid geometry. Fall back to surface dimensions.
    if (geom.width == 0)  geom.width  = client->xdg_surface->surface->current.width  - geom.x;
    if (geom.height == 0) geom.height = client->xdg_surface->surface->current.height - geom.y;

    return geom;
}

wlr_box client_get_coord_system(Client* client)
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

wlr_box client_get_bounds(Client* client)
{
    wlr_box box = client_get_geometry(client);
    wlr_scene_node_coords(&client->scene_tree->node, &box.x, &box.y);
    return box;
}

void toplevel_resize(Toplevel* toplevel, int width, int height)
{
    if (toplevel->resize.enable_throttle_resize && toplevel->resize.last_resize_serial > toplevel->resize.last_commited_serial) {
        if (!toplevel->resize.any_pending || width != toplevel->resize.pending_width || height != toplevel->resize.pending_height) {

#if NOISY_RESIZE
            wlr_log(WLR_INFO, "resize.pending[%i > %i] (%i, %i) -> (%i, %i)",
                toplevel->resize.last_resize_serial, toplevel->resize.last_commited_serial,
                toplevel->resize.pending_width, toplevel->resize.pending_height, width, height);
#endif

            toplevel->resize.any_pending = true;
            toplevel->resize.pending_width = width;
            toplevel->resize.pending_height = height;
        }
    } else {
        toplevel->resize.any_pending = false;

        if (toplevel->xdg_toplevel->pending.width != width || toplevel->xdg_toplevel->pending.height != height) {
#if NOISY_RESIZE
            wlr_log(WLR_INFO, "resize.request[%i] (%i, %i) -> (%i, %i)", toplevel->resize.last_resize_serial,
                toplevel->xdg_toplevel->pending.width, toplevel->xdg_toplevel->pending.height,
                width,                                 height);
#endif

            toplevel->resize.last_resize_serial = wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);
        }
    }
}

static
void toplevel_resize_handle_commit(Toplevel* toplevel)
{
    if (toplevel->resize.last_commited_serial == toplevel->xdg_toplevel->base->current.configure_serial) return;
    toplevel->resize.last_commited_serial = toplevel->xdg_toplevel->base->current.configure_serial;

    if (toplevel->resize.last_commited_serial < toplevel->resize.last_resize_serial) return;
    toplevel->resize.last_resize_serial = toplevel->resize.last_commited_serial;

#if NOISY_RESIZE
    {
        wlr_box box = client_get_geometry(toplevel);
        int buffer_width = toplevel->xdg_surface->surface->current.buffer_width;
        int buffer_height = toplevel->xdg_surface->surface->current.buffer_height;

        wlr_log(WLR_INFO, "resize_handle_commit geom = (%i, %i), toplevel = (%i, %i), buffer = (%i, %i)",
            box.width, box.height,
            toplevel->xdg_toplevel->current.width, toplevel->xdg_toplevel->current.height,
            buffer_width, buffer_height);
    }
#endif

    wlr_box bounds = client_get_bounds(toplevel);
#if NOISY_RESIZE
    wlr_log(WLR_INFO, "resize.commit[%i] (%i, %i)", toplevel->resize.last_commited_serial, bounds.width, bounds.height);
#endif

    if (toplevel->resize.any_pending) {
#if NOISY_RESIZE
        wlr_log(WLR_INFO, "  found pending resizes, sending new resize");
#endif
        toplevel->resize.any_pending = false;
        toplevel_resize(toplevel, toplevel->resize.pending_width, toplevel->resize.pending_height);
    } else {
        if (bounds.width != toplevel->xdg_toplevel->current.width || bounds.height != toplevel->xdg_toplevel->current.height) {
            // Client has committed geometry that doesn't match our requested size
            // Resize toplevel to match committed geometry (client authoritative)
#if NOISY_RESIZE
            wlr_log(WLR_INFO, "  no pending resizes, new bounds don't match requested toplevel size, overriding toplevel size (client authoritative)");
#endif
            toplevel_resize(toplevel, bounds.width, bounds.height);
        }
    }
}

void toplevel_set_bounds(Toplevel* toplevel, wlr_box box)
{
    // NOTE: Bounds are set with parent node relative positions, unlike get_bounds which returns layout relative positions
    //       Thus you must be careful when setting/getting bounds with positioned parents
    // TODO: Tidy up this API and make it clear what is relative to what.
    wlr_scene_node_set_position(&toplevel->scene_tree->node, box.x, box.y);

    toplevel_resize(toplevel, box.width, box.height);
}

bool toplevel_wants_fullscreen(Toplevel* toplevel)
{
    return toplevel->xdg_toplevel->requested.fullscreen;
}

void toplevel_set_fullscreen(Toplevel* toplevel, bool fullscreen)
{
    if (fullscreen) {
        wlr_box prev = client_get_bounds(toplevel);
        Output* output = get_output_for_client(toplevel);
        if (output) {
            wlr_box b = output_get_bounds(output);
            wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, true);
            toplevel_set_bounds(toplevel, b);
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
        toplevel_set_bounds(toplevel, toplevel->prev_bounds);
    }
}

void toplevel_set_activated(Toplevel* toplevel, bool active)
{
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, active);
    toplevel_update_border(toplevel);
}

void cycle_focus_immediate(Server* server, wlr_cursor* cursor, bool backwards)
{
    auto in_cycle = [&](Client* client) {
        return !cursor || wlr_box_contains_point(ptr(client_get_bounds(client)), cursor->x, cursor->y);
    };

    if (backwards) {
        for (Toplevel* toplevel : server->toplevels) {
            if (toplevel == server->focused_toplevel) continue;
            if (!in_cycle(toplevel)) continue;

            toplevel_focus(toplevel);
            return;
        }
    } else {
        uint32_t i = server->toplevels.size();
        while (i-- > 0) {
            Toplevel* toplevel = server->toplevels[i];
            if (toplevel == server->focused_toplevel) continue;
            if (!in_cycle(toplevel)) continue;

            if (server->focused_toplevel && in_cycle(server->focused_toplevel)) {
                // Re-insert currently focused window at bottom of focus stack if in cycle
                std::erase(server->toplevels, server->focused_toplevel);
                server->toplevels.insert(server->toplevels.begin(), server->focused_toplevel);
            }

            // Focus new window
            toplevel_focus(toplevel);
            return;
        }
    }
}

void toplevel_focus(Toplevel* toplevel)
{
    // NOTE: This function only deals with keyboard focus

    if (!toplevel) return;

    Server* server = toplevel->server;
    wlr_seat* seat = server->seat;
    wlr_surface* prev_surface = seat->keyboard_state.focused_surface;
    wlr_surface* surface = toplevel_get_surface(toplevel);

    server->focused_toplevel = toplevel;

    if (prev_surface == surface) return;

    if (prev_surface) {
        wlr_xdg_toplevel* prev_toplevel = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel) {
            toplevel_set_activated(static_cast<Toplevel*>(prev_toplevel->base->data), false);
        }
    }

    wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    std::erase(server->toplevels, toplevel);
    server->toplevels.emplace_back(toplevel);
    toplevel_set_activated(toplevel, true);

    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }

    toplevel_update_border(toplevel);
}

void toplevel_unfocus(Toplevel* toplevel)
{
    if (!toplevel) return;
    if (toplevel->server->focused_toplevel != toplevel)
        return;

    toplevel->server->focused_toplevel = nullptr;
    wlr_seat_keyboard_notify_clear_focus(toplevel->server->seat);

    toplevel_update_border(toplevel);
}

Toplevel* get_toplevel_at(Server* server, double lx, double ly, wlr_surface** surface, double *sx, double *sy)
{
    // This returns the topmost node in the scene at the given layout coords.
    // We only care about surface nodes as we are specifically looking for a surface in the surface tree of a client

    wlr_scene_node* node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) return nullptr;

    wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
    wlr_scene_surface* scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) return nullptr;

    *surface = scene_surface->surface;

    wlr_scene_tree* tree = node->parent;
    while (tree && (!tree->node.data || static_cast<Client*>(tree->node.data)->type != ClientType::toplevel)) {
        tree = tree->node.parent;
    }

    return tree ? static_cast<Toplevel*>(tree->node.data) : nullptr;
}

void toplevel_map(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    toplevel->server->toplevels.emplace_back(toplevel);

    toplevel_focus(toplevel);
}

void toplevel_unmap(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    // Reset cursor mode if grabbed toplevel was unmapped
    if (toplevel == toplevel->server->movesize.grabbed_toplevel) {
        reset_cursor_mode(toplevel->server);
    }

    // TODO: Handle toplevel unmap during zone operation

    toplevel_unfocus(toplevel);
    std::erase(toplevel->server->toplevels, toplevel);
    if (!toplevel->server->toplevels.empty()) {
        toplevel_focus(toplevel->server->toplevels.back());
    }
}

void toplevel_commit(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    if (toplevel->xdg_toplevel->base->initial_commit) {
        decoration_set_mode(toplevel);
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
    } else {
        toplevel_resize_handle_commit(toplevel);
        toplevel_update_border(toplevel);
    }
}

void toplevel_destroy(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    delete toplevel;
}

bool toplevel_is_interactable(Toplevel* toplevel)
{
    if (toplevel->xdg_toplevel->current.fullscreen) return false;

    return true;
}

void toplevel_begin_interactive(Toplevel* toplevel, CursorMode mode, uint32_t edges)
{
    if (!toplevel_is_interactable(toplevel)) return;

    Server* server = toplevel->server;

    server->movesize.grabbed_toplevel = toplevel;
    server->cursor_mode = mode;

    if (mode == CursorMode::move) {
        server->movesize.grab_x = server->cursor->x - toplevel->scene_tree->node.x;
        server->movesize.grab_y = server->cursor->y - toplevel->scene_tree->node.y;
    } else {
        wlr_box geom = toplevel->xdg_toplevel->base->geometry;

        double border_x = (toplevel->scene_tree->node.x + geom.x) + ((edges & WLR_EDGE_RIGHT ) ? geom.width  : 0);
        double border_y = (toplevel->scene_tree->node.y + geom.y) + ((edges & WLR_EDGE_BOTTOM) ? geom.height : 0);

        server->movesize.grab_x = server->cursor->x - border_x;
        server->movesize.grab_y = server->cursor->y - border_y;

        server->movesize.grab_geobox = geom;
        server->movesize.grab_geobox.x += toplevel->scene_tree->node.x;
        server->movesize.grab_geobox.y += toplevel->scene_tree->node.y;

        server->movesize.resize_edges = edges;
    }
}

void toplevel_request_maximize(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    // NOTE: We won't support maximization, but we have to send a configure event anyway

    if (toplevel->xdg_toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
    }
}

void toplevel_request_fullscreen(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    if (toplevel->xdg_toplevel->base->initialized) {
        toplevel_set_fullscreen(toplevel, toplevel_wants_fullscreen(toplevel));
    }
}

void server_new_toplevel(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_xdg_toplevel* xdg_toplevel = static_cast<wlr_xdg_toplevel*>(data);

    Toplevel* toplevel = new Toplevel{};
    toplevel->type = ClientType::toplevel;
    toplevel->server = server;
    toplevel->xdg_surface = xdg_toplevel->base;
    toplevel->xdg_surface->data = toplevel;

    toplevel->xdg_toplevel = xdg_toplevel;
    toplevel->scene_tree = wlr_scene_xdg_surface_create(&toplevel->server->scene->tree, xdg_toplevel->base);
    toplevel->scene_tree->node.data = toplevel;

    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.map,    toplevel, toplevel_map);
    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.unmap,  toplevel, toplevel_unmap);
    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.commit, toplevel, toplevel_commit);

    toplevel->listeners.listen(&xdg_toplevel->events.destroy, toplevel, toplevel_destroy);

    toplevel->listeners.listen(&xdg_toplevel->events.request_maximize,   toplevel, toplevel_request_maximize);
    toplevel->listeners.listen(&xdg_toplevel->events.request_fullscreen, toplevel, toplevel_request_fullscreen);

    for (int i = 0; i < 4; ++i) {
        toplevel->border[i] = wlr_scene_rect_create(toplevel->scene_tree, 0, 0, border_color_unfocused.values);
    }
}

// -----------------------------------------------------------------------------

void decoration_set_mode(Toplevel* toplevel)
{
    if (!toplevel->decoration.xdg_decoration) return;

    if (toplevel->xdg_toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration.xdg_decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
}

void decoration_new(wl_listener*, void* data)
{
    wlr_xdg_toplevel_decoration_v1* xdg_decoration = static_cast<wlr_xdg_toplevel_decoration_v1*>(data);

    Toplevel* toplevel = static_cast<Toplevel*>(xdg_decoration->toplevel->base->data);
    if (toplevel->decoration.xdg_decoration) {
        wlr_log(WLR_ERROR, "Toplevel already has attached decoration!");
        return;
    }

    toplevel->decoration.xdg_decoration = xdg_decoration;

    toplevel->decoration.listeners.listen(&xdg_decoration->events.request_mode, toplevel, decoration_request_mode);
    toplevel->decoration.listeners.listen(&xdg_decoration->events.destroy,      toplevel, decoration_destroy);

    decoration_set_mode(toplevel);
}

void decoration_request_mode(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);
    decoration_set_mode(toplevel);
}

void decoration_destroy(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    toplevel->decoration.xdg_decoration = nullptr;
    toplevel->decoration.listeners.clear();
}

// -----------------------------------------------------------------------------

void popup_commit(wl_listener* listener, void*)
{
    Popup* popup = listener_userdata<Popup*>(listener);

    wlr_xdg_popup* xdg_popup = popup->xdg_popup;

    if (!xdg_popup->base->initial_commit) {
        return;
    }

    wlr_xdg_surface* xdg_parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    Client* parent = static_cast<Client*>(xdg_parent->data);
    popup->scene_tree = wlr_scene_xdg_surface_create(parent->scene_tree, xdg_popup->base);

    Output* output = get_output_for_client(parent);
    if (output) {
        wlr_box output_bounds = output_get_bounds(output);

        {
            // TODO: Move this into a helper for getting a toplevel from an wlr_(xdg_)surface
            Client* cur = parent;
            while (cur->type == ClientType::popup) {
                cur = static_cast<Client*>(wlr_xdg_surface_try_from_wlr_surface(static_cast<Popup*>(cur)->xdg_popup->parent)->data);
            }

            // Adjust output bounds to be in the root toplevel's surface coordinate system.
            wlr_box coord_system = client_get_coord_system(cur);
            output_bounds.x -= coord_system.x;
            output_bounds.y -= coord_system.y;
        }

        wlr_xdg_popup_unconstrain_from_box(xdg_popup, &output_bounds);
    } else {
        wlr_log(WLR_ERROR, "No output for toplevel while opening popup!");
    }
}

void popup_destroy(wl_listener* listener, void*)
{
    Popup* popup = listener_userdata<Popup*>(listener);

    delete popup;
}

void server_new_popup(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_xdg_popup* xdg_popup = static_cast<wlr_xdg_popup*>(data);

    Popup* popup = new Popup{};
    popup->type = ClientType::popup;
    popup->server = server;
    popup->xdg_surface = xdg_popup->base;
    popup->xdg_surface->data = popup;

    popup->xdg_popup = xdg_popup;

    popup->listeners.listen(&xdg_popup->base->surface->events.commit,  popup, popup_commit);
    popup->listeners.listen(&               xdg_popup->events.destroy, popup, popup_destroy);
}
