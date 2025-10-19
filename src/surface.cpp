#include "pch.hpp"
#include "core.hpp"

Surface* get_focused_surface(Server* server)
{
    return Surface::from(server->seat->keyboard_state.focused_surface);
}

void toplevel_update_border(Toplevel* toplevel)
{
    static constexpr uint32_t left = 0;
    static constexpr uint32_t top = 1;
    static constexpr uint32_t right = 2;
    static constexpr uint32_t bottom = 3;

    wlr_box bounds = surface_get_bounds(toplevel);

    bool show = bounds.width && bounds.height;
    show &= !toplevel->xdg_toplevel()->current.fullscreen;

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
                get_focused_surface(toplevel->server) == toplevel
                    ? border_color_focused.values
                    : border_color_unfocused.values);
        } else {
            wlr_scene_node_set_enabled(&toplevel->border[i]->node, false);
        }
    }
}

wlr_box surface_get_geometry(Surface* surface)
{
    wlr_box geom = {};
    if (wlr_xdg_surface* xdg_surface = wlr_xdg_surface_try_from_wlr_surface(surface->wlr_surface)) {
        geom = xdg_surface->current.geometry;

        // Some clients (E.g. SDL3 + Vulkan) fail to report valid geometry. Fall back to surface dimensions.
        if (geom.width == 0)  geom.width  = xdg_surface->surface->current.width  - geom.x;
        if (geom.height == 0) geom.height = xdg_surface->surface->current.height - geom.y;
    } else  if (wlr_layer_surface_v1* wlr_layer_surface = wlr_layer_surface_v1_try_from_wlr_surface(surface->wlr_surface)) {
        geom.width = wlr_layer_surface->current.actual_width;
        geom.height = wlr_layer_surface->current.actual_height;
    } else {
        log_error("failed to get surface geometry");
    }

    return geom;
}

wlr_box surface_get_coord_system(Surface* surface)
{
    wlr_box box = {};
    if (surface->scene_tree) {
        wlr_scene_node_coords(&surface->scene_tree->node, &box.x, &box.y);
    }

    if (wlr_xdg_surface* xdg_surface = wlr_xdg_surface_try_from_wlr_surface(surface->wlr_surface)) {

        // Scene node coordinates position the geometry origin, adjust to surface origin
        box.x -= xdg_surface->current.geometry.x;
        box.y -= xdg_surface->current.geometry.y;

        box.width = xdg_surface->surface->current.width;
        box.height = xdg_surface->surface->current.height;
    }

    if (wlr_layer_surface_v1* wlr_layer_surface = wlr_layer_surface_v1_try_from_wlr_surface(surface->wlr_surface)) {
        box.width = wlr_layer_surface->current.actual_width;
        box.height = wlr_layer_surface->current.actual_height;
    }

    return box;
}

wlr_box surface_get_bounds(Surface* surface)
{
    wlr_box box = surface_get_geometry(surface);
    wlr_scene_node_coords(&surface->scene_tree->node, &box.x, &box.y);
    return box;
}

void toplevel_resize(Toplevel* toplevel, int width, int height)
{
    if (toplevel->resize.enable_throttle_resize && toplevel->resize.last_resize_serial > toplevel->resize.last_commited_serial) {
        if (!toplevel->resize.any_pending || width != toplevel->resize.pending_width || height != toplevel->resize.pending_height) {

#if NOISY_RESIZE
            log_debug("resize.pending[{} > {}] ({}, {}) -> ({}, {})",
                toplevel->resize.last_resize_serial, toplevel->resize.last_commited_serial,
                toplevel->resize.pending_width, toplevel->resize.pending_height, width, height);
#endif

            toplevel->resize.any_pending = true;
            toplevel->resize.pending_width = width;
            toplevel->resize.pending_height = height;
        }
    } else {
        toplevel->resize.any_pending = false;

        if (toplevel->xdg_toplevel()->pending.width != width || toplevel->xdg_toplevel()->pending.height != height) {
#if NOISY_RESIZE
            log_debug("resize.request[{}] ({}, {}) -> ({}, {})", toplevel->resize.last_resize_serial,
                toplevel->xdg_toplevel->pending.width, toplevel->xdg_toplevel->pending.height,
                width,                                 height);
#endif

            toplevel->resize.last_resize_serial = wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel(), width, height);
        }
    }
}

static
void toplevel_resize_handle_commit(Toplevel* toplevel)
{
    if (toplevel->resize.last_commited_serial == toplevel->xdg_toplevel()->base->current.configure_serial) return;
    toplevel->resize.last_commited_serial = toplevel->xdg_toplevel()->base->current.configure_serial;

    if (toplevel->resize.last_commited_serial < toplevel->resize.last_resize_serial) return;
    toplevel->resize.last_resize_serial = toplevel->resize.last_commited_serial;

    // Update cursor focus if window under cursor has changed
    process_cursor_motion(toplevel->server, 0, nullptr, 0, 0, 0, 0);

#if NOISY_RESIZE
    {
        wlr_box box = surface_get_geometry(toplevel);
        int buffer_width = toplevel->xdg_surface->surface->current.buffer_width;
        int buffer_height = toplevel->xdg_surface->surface->current.buffer_height;

        log_debug("resize_handle_commit geom = ({}, {}), toplevel = ({}, {}), buffer = ({}, {})",
            box.width, box.height,
            toplevel->xdg_toplevel->current.width, toplevel->xdg_toplevel->current.height,
            buffer_width, buffer_height);
    }
#endif

    wlr_box bounds = surface_get_bounds(toplevel);
#if NOISY_RESIZE
    log_debug("resize.commit[{}] ({}, {})", toplevel->resize.last_commited_serial, bounds.width, bounds.height);
#endif

    if (toplevel->resize.any_pending) {
#if NOISY_RESIZE
        log_debug("  found pending resizes, sending new resize");
#endif
        toplevel->resize.any_pending = false;
        toplevel_resize(toplevel, toplevel->resize.pending_width, toplevel->resize.pending_height);
    } else {
        if (bounds.width != toplevel->xdg_toplevel()->current.width || bounds.height != toplevel->xdg_toplevel()->current.height) {
            // Client has committed geometry that doesn't match our requested size
            // Resize toplevel to match committed geometry (client authoritative)
#if NOISY_RESIZE
            log_debug("  no pending resizes, new bounds don't match requested toplevel size, overriding toplevel size (client authoritative)");
#else
            log_warn("Overriding requested toplevel size ({}, {}) with surface size ({}, {})",
                toplevel->xdg_toplevel()->current.width, toplevel->xdg_toplevel()->current.height, bounds.width, bounds.height);
#endif
            toplevel_resize(toplevel, bounds.width, bounds.height);
        }
    }
}

void toplevel_set_bounds(Toplevel* toplevel, wlr_box box)
{
    if (toplevel->xdg_toplevel()->current.maximized) {
        wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel(), false);
    }

    // NOTE: Bounds are set with parent node relative positions, unlike get_bounds which returns layout relative positions
    //       Thus you must be careful when setting/getting bounds with positioned parents
    // TODO: Tidy up this API and make it clear what is relative to what.
    wlr_scene_node_set_position(&toplevel->scene_tree->node, box.x, box.y);

    // Match popup tree location
    int x, y;
    wlr_scene_node_coords(&toplevel->scene_tree->node, &x, &y);
    wlr_scene_node_set_position(&toplevel->popup_tree->node, x, y);

    toplevel_resize(toplevel, box.width, box.height);
}

void toplevel_set_fullscreen(Toplevel* toplevel, bool fullscreen)
{
    if (fullscreen) {
        wlr_box prev = surface_get_bounds(toplevel);
        Output* output = get_output_for_surface(toplevel);
        if (output) {
            wlr_box b = output_get_bounds(output);
            wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel(), true);
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
        wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel(), false);

        // Constrain prev bounds to output when exiting fullscreen to avoid the case
        // where the window is still full size and the borders are now hidden.
        if (Output* output = get_nearest_output_to_box(toplevel->server, toplevel->prev_bounds)) {
            wlr_box output_bounds = zone_apply_external_padding(output->workarea);
            toplevel->prev_bounds = constrain_box(toplevel->prev_bounds, output_bounds);
        }
        toplevel_set_bounds(toplevel, toplevel->prev_bounds);
    }
}

void toplevel_set_activated(Toplevel* toplevel, bool active)
{
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel(), active);
    toplevel_update_border(toplevel);
}

// -----------------------------------------------------------------------------

static
std::string toplevel_debug_get_name(Toplevel* toplevel)
{
    return toplevel
        ? std::format("Toplevel<{}>({}, {})",
            (void*)toplevel,
            toplevel->xdg_toplevel()->app_id ? toplevel->xdg_toplevel()->app_id : "?",
            toplevel->xdg_toplevel()->title ? toplevel->xdg_toplevel()->title   : "?")
        : "";
}

static
void walk_toplevels(Server* server, bool(*for_each)(void*, Toplevel*), void* for_each_data, bool backward)
{
    auto fn = [&](wlr_scene_node* node, double, double) -> bool {
        if (Toplevel* toplevel = Toplevel::from(node)) {
            if (&toplevel->scene_tree->node != node) {
                // TODO: Root cause this issue in wlroots
                log_error("BUG - Unexpected wlr_scene_node referencing {} (expected {}, got {}), unlinking!",
                    toplevel_debug_get_name(toplevel),
                    (void*)&toplevel->scene_tree->node,
                    (void*)node);
                node->data = nullptr;
                return true;
            }
            return for_each(for_each_data, toplevel);
        }
        return true;
    };
    if (backward) {
        walk_scene_tree_back_to_front(&server->scene->tree.node, 0, 0, FUNC_REF(fn));
    } else {
        walk_scene_tree_front_to_back(&server->scene->tree.node, 0, 0, FUNC_REF(fn));
    }
}

static
void walk_toplevels_front_to_back(Server* server, bool(*for_each)(void*, Toplevel*), void* for_each_data)
{
    walk_toplevels(server, for_each, for_each_data, false);
}

static
void walk_toplevels_back_to_front(Server* server, bool(*for_each)(void*, Toplevel*), void* for_each_data)
{
    walk_toplevels(server, for_each, for_each_data, true);
}

// -----------------------------------------------------------------------------

static
bool focus_cycle_toplevel_is_enabled(Toplevel* toplevel)
{
    return toplevel->scene_tree->node.enabled;
}

static
void focus_cycle_toplevel_set_enabled(Toplevel* toplevel, bool enabled)
{
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, enabled);
}

void focus_cycle_begin(Server* server, wlr_cursor* cursor)
{
    set_interaction_mode(server, InteractionMode::focus_cycle);

    surface_unfocus(get_focused_surface(server), true);

    Toplevel* current = nullptr;
    auto fn = [&](Toplevel* toplevel) -> bool {
        bool new_current = !current && (!cursor || wlr_box_contains_point(ptr(surface_get_bounds(toplevel)), cursor->x, cursor->y));
        focus_cycle_toplevel_set_enabled(toplevel, new_current);
        if (new_current) current = toplevel;
        return true;
    };
    walk_toplevels_front_to_back(server, FUNC_REF(fn));
}

void focus_cycle_end(Server* server)
{
    if (server->interaction_mode != InteractionMode::focus_cycle) return;
    server->interaction_mode = InteractionMode::passthrough;

    Toplevel* focused = nullptr;
    auto fn = [&](Toplevel* toplevel) -> bool {
        if (focus_cycle_toplevel_is_enabled(toplevel)) {
            if (!focused) {
                focused = toplevel;
            }
        }

        focus_cycle_toplevel_set_enabled(toplevel, true);
        return true;
    };
    walk_toplevels_front_to_back(server, FUNC_REF(fn));

    surface_focus(focused);
}

void focus_cycle_step(Server* server, wlr_cursor* cursor, bool backwards)
{
    // TODO: Fixup visibility if focus changes mid-cycle (e.g. window added/destroyed)

    Toplevel* first = nullptr;
    bool next_is_active = false;
    Toplevel* new_active = nullptr;

    auto iter_fn = backwards ? walk_toplevels_back_to_front : walk_toplevels_front_to_back;

    auto pick = [&](Toplevel* toplevel) -> bool {
        bool in_cycle = !cursor || wlr_box_contains_point(ptr(surface_get_bounds(toplevel)), cursor->x, cursor->y);
        if (!in_cycle) return true;

        // Is this is the first window in the cycle, mark incase we need to
        if (!first) first = toplevel;

        // Previous window was active, moving to this one
        if (next_is_active) {
            new_active = toplevel;
            return false;
        }

        // This is the first active window in the cycle, cycle to next
        if (focus_cycle_toplevel_is_enabled(toplevel)) {
            next_is_active = true;
        }

        return true;
    };
    iter_fn(server, FUNC_REF(pick));

    if (!new_active && first) {
        new_active = first;
    }

    auto update = [&](Toplevel* toplevel) -> bool {
        focus_cycle_toplevel_set_enabled(toplevel, toplevel == new_active);
        return true;
    };
    iter_fn(server, FUNC_REF(update));
}

// -----------------------------------------------------------------------------

void surface_focus(Surface* surface)
{
    if (!surface) return;

    Server* server = surface->server;
    wlr_seat* seat = server->seat;
    wlr_surface* prev_wlr_surface = seat->keyboard_state.focused_surface;
    struct wlr_surface* wlr_surface = surface->wlr_surface;

    // TODO: This causes issues when server.focused_toplevel and keyboard.focused_surface are desyncd
    if (prev_wlr_surface == wlr_surface) return;

    if (Toplevel* prev_toplevel = Toplevel::from(prev_wlr_surface)) {
        toplevel_set_activated(prev_toplevel, false);
    }

    Toplevel* toplevel = Toplevel::from(surface);

    wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    if (toplevel) {
        wlr_scene_node_raise_to_top(&surface->scene_tree->node);
        toplevel_set_activated(Toplevel::from(surface), true);
    }

    if (surface->wlr_surface != server->seat->keyboard_state.focused_surface) {
        // Focusing surface that didn't previously have keyboard focus, clear pointer to force a cursor surface update
        wlr_seat_pointer_clear_focus(server->seat);
        seat_reset_cursor(surface->server);
    }

    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, wlr_surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }

    // TODO: Tidy up and consolidate API for handling (re)focus
    process_cursor_motion(server, 0, nullptr, 0, 0, 0, 0);
}

void surface_unfocus(Surface* surface, bool force)
{
    if (!surface) return;
    if (get_focused_surface(surface->server) != surface)
        return;


    if (force) {
        wlr_seat_keyboard_clear_focus(surface->server->seat);
    } else {
        wlr_seat_keyboard_notify_clear_focus(surface->server->seat);
    }

    seat_reset_cursor(surface->server);
}

Surface* get_surface_at(Server* server, double lx, double ly, wlr_surface** p_surface, double *p_sx, double *p_sy)
{
    Surface* surface = nullptr;
    auto fn = [&](wlr_scene_node* node, double node_x, double node_y) {
        if (!node->enabled || node->type != WLR_SCENE_NODE_BUFFER) return true;

        wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
        wlr_scene_surface* scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
        if (!scene_surface) return true;
        if (lx < node_x || ly < node_y || lx - node_x >= scene_buffer->dst_width || ly - node_y >= scene_buffer->dst_height) return true;

        *p_surface = scene_surface->surface;
        *p_sx = lx - node_x;
        *p_sy = ly - node_y;

        if (scene_buffer->point_accepts_input && !scene_buffer->point_accepts_input(scene_buffer, p_sx, p_sy)) {
            return true;
        }

        wlr_scene_tree* tree = node->parent;
        while (tree && !(surface = Surface::from(&tree->node))) {
            tree = tree->node.parent;
        }

        return !tree;
    };
    walk_scene_tree_front_to_back(&server->scene->tree.node, 0, 0, FUNC_REF(fn));

    return surface;
}

void toplevel_map(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    log_debug("Toplevel mapped:    {}", toplevel_debug_get_name(toplevel));

    surface_focus(toplevel);
}

void toplevel_unmap(wl_listener* listener, void*)
{
    Toplevel* unmapped_toplevel = listener_userdata<Toplevel*>(listener);

    log_debug("Toplevel unmapped:  {}", toplevel_debug_get_name(unmapped_toplevel));

    // Reset interaction mode if grabbed toplevel was unmapped
    if (unmapped_toplevel == unmapped_toplevel->server->movesize.grabbed_toplevel) {
        set_interaction_mode(unmapped_toplevel->server, InteractionMode::passthrough);
    }

    // TODO: Handle toplevel unmap during zone operation

    if (get_focused_surface(unmapped_toplevel->server) == unmapped_toplevel) {
        surface_unfocus(unmapped_toplevel, true);

        auto fn = [&](Toplevel* toplevel) {
            if (toplevel != unmapped_toplevel) {
                surface_focus(toplevel);
                return false;
            }
            return true;
        };
        walk_toplevels_front_to_back(unmapped_toplevel->server, FUNC_REF(fn));
    }
}

void toplevel_commit(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    if (toplevel->xdg_toplevel()->base->initial_commit) {

        log_info("Toplevel committed: {}", toplevel_debug_get_name(toplevel));

        decoration_set_mode(toplevel);
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel(), 0, 0);
    } else {
        if (toplevel->xdg_toplevel()->current.width == 0) {
            // Toplevel initial commit response

            wlr_box bounds = surface_get_bounds(toplevel);

            if (toplevel->xdg_toplevel()->parent) {
                // Child, position at center of parent.
                // TODO: Use xdg_positioner requests to position child windwos
                wlr_box parent_bounds = surface_get_bounds(Surface::from(toplevel->xdg_toplevel()->parent->base->surface));
                bounds.x = parent_bounds.x + (parent_bounds.width - bounds.width)   / 2;
                bounds.y = parent_bounds.y + (parent_bounds.height - bounds.height) / 2;
            } else {
                // Non-child, spawn under mouse
                bounds.x = toplevel->server->cursor->x - bounds.width  / 2.0;
                bounds.y = toplevel->server->cursor->y - bounds.height / 2.0;
            }

            // Constrain to output (respecting external padding)
            Output* output = get_nearest_output_to_box(toplevel->server, bounds);
            if (output) {
                bounds = constrain_box(bounds, zone_apply_external_padding(output->workarea));
            }

            // Update toplevel bounds
            toplevel_set_bounds(toplevel, bounds);
        }

        toplevel_resize_handle_commit(toplevel);
        toplevel_update_border(toplevel);
    }
}

void toplevel_destroy(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    log_debug("Toplevel destroyed: {} (wlr_surface = {}, xdg_toplevel = {}, scene_tree.node = {})",
        toplevel_debug_get_name(toplevel),
        (void*)toplevel->xdg_toplevel()->base->surface,
        (void*)toplevel->xdg_toplevel(),
        (void*)&toplevel->scene_tree->node);

    wlr_scene_node_destroy(&toplevel->popup_tree->node);

    toplevel->wlr_surface->data = nullptr;

    delete toplevel;
}

bool toplevel_is_interactable(Toplevel* toplevel)
{
    if (toplevel->xdg_toplevel()->current.fullscreen) return false;

    return true;
}

void toplevel_begin_interactive(Toplevel* toplevel, InteractionMode mode)
{
    if (!toplevel_is_interactable(toplevel)) return;

    Server* server = toplevel->server;

    uint32_t edges = 0;
    if (mode == InteractionMode::resize) {
        wlr_box bounds = surface_get_bounds(toplevel);
        int nine_slice_x = ((server->cursor->x - bounds.x) * 3) / bounds.width;
        int nine_slice_y = ((server->cursor->y - bounds.y) * 3) / bounds.height;

        if      (nine_slice_x < 1) edges |= WLR_EDGE_LEFT;
        else if (nine_slice_x > 1) edges |= WLR_EDGE_RIGHT;

        if      (nine_slice_y < 1) edges |= WLR_EDGE_TOP;
        else if (nine_slice_y > 1) edges |= WLR_EDGE_BOTTOM;

        // If no edges selected, must be center - switch to move
        if (!edges) mode = InteractionMode::move;
    }

    server->movesize.grabbed_toplevel = toplevel;
    set_interaction_mode(server, mode);

    if (mode == InteractionMode::move) {
        server->movesize.grab = Point{server->cursor->x, server->cursor->y};
        server->movesize.grab_bounds = surface_get_bounds(toplevel);
    } else {
        server->movesize.grab = Point{server->cursor->x, server->cursor->y};
        server->movesize.grab_bounds = surface_get_bounds(toplevel);
        server->movesize.resize_edges = edges;
    }
}

void toplevel_request_minimize(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    if (toplevel->xdg_toplevel()->base->initialized) {
        // We don't support minimize, send empty configure
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel()->base);
    }
}

void toplevel_request_maximize(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    if (toplevel->xdg_toplevel()->base->initialized) {
        if (toplevel->xdg_toplevel()->requested.maximized) {
            toplevel->prev_bounds = surface_get_bounds(toplevel);
            if (Output* output = get_nearest_output_to_box(toplevel->server, toplevel->prev_bounds)) {
                toplevel_set_bounds(toplevel, zone_apply_external_padding(output->workarea));
                wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel(), true);
            }
        } else {
            if (Output* output = get_nearest_output_to_box(toplevel->server, toplevel->prev_bounds)) {
                toplevel->prev_bounds = constrain_box(toplevel->prev_bounds, zone_apply_external_padding(output->workarea));
            }
            toplevel_set_bounds(toplevel, toplevel->prev_bounds);
        }
    }
}

void toplevel_request_fullscreen(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    if (toplevel->xdg_toplevel()->base->initialized) {
        toplevel_set_fullscreen(toplevel, toplevel->xdg_toplevel()->requested.fullscreen);
    }
}

void server_new_toplevel(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_xdg_toplevel* xdg_toplevel = static_cast<wlr_xdg_toplevel*>(data);

    Toplevel* toplevel = new Toplevel{};
    toplevel->role = SurfaceRole::toplevel;
    toplevel->server = server;
    toplevel->wlr_surface = xdg_toplevel->base->surface;
    toplevel->wlr_surface->data = toplevel;

    toplevel->scene_tree = wlr_scene_xdg_surface_create(toplevel->server->layers[Strata::floating], xdg_toplevel->base);
    toplevel->scene_tree->node.data = toplevel;

    log_debug("Toplevel created:   {} (wlr_surface = {}, xdg_toplevel = {}, scene_tree.node = {})",
        toplevel_debug_get_name(toplevel),
        (void*)xdg_toplevel->base->surface,
        (void*)xdg_toplevel,
        (void*)&toplevel->scene_tree->node);

    toplevel->popup_tree = wlr_scene_tree_create(server->layers[Strata::top]);

    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.map,    toplevel, toplevel_map);
    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.unmap,  toplevel, toplevel_unmap);
    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.commit, toplevel, toplevel_commit);

    toplevel->listeners.listen(&xdg_toplevel->events.destroy, toplevel, toplevel_destroy);

    toplevel->listeners.listen(&xdg_toplevel->events.request_maximize,   toplevel, toplevel_request_maximize);
    toplevel->listeners.listen(&xdg_toplevel->events.request_minimize,   toplevel, toplevel_request_minimize);
    toplevel->listeners.listen(&xdg_toplevel->events.request_fullscreen, toplevel, toplevel_request_fullscreen);

    for (int i = 0; i < 4; ++i) {
        toplevel->border[i] = wlr_scene_rect_create(toplevel->scene_tree, 0, 0, border_color_unfocused.values);
    }
}

// -----------------------------------------------------------------------------

void decoration_set_mode(Toplevel* toplevel)
{
    if (!toplevel->decoration.xdg_decoration) return;

    if (toplevel->xdg_toplevel()->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration.xdg_decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
}

void decoration_new(wl_listener*, void* data)
{
    wlr_xdg_toplevel_decoration_v1* xdg_decoration = static_cast<wlr_xdg_toplevel_decoration_v1*>(data);

    Toplevel* toplevel = Toplevel::from(xdg_decoration->toplevel->base->surface);
    if (toplevel->decoration.xdg_decoration) {
        log_error("Toplevel already has attached decoration!");
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

void output_layout_layer(Output* output, zwlr_layer_shell_v1_layer layer)
{
    wlr_box full_area = output_get_bounds(output);

    for (LayerSurface* layer_surface : output->layers[layer]) {
        if (!layer_surface->wlr_layer_surface()->initialized) continue;

        wlr_scene_layer_surface_v1_configure(layer_surface->scene_layer_surface, &full_area, &output->workarea);
        wlr_scene_node_set_position(&layer_surface->popup_tree->node, layer_surface->scene_tree->node.x, layer_surface->scene_tree->node.y);
    }
}

static
void layer_surface_commit(wl_listener* listener, void*)
{
    LayerSurface* layer_surface = listener_userdata<LayerSurface*>(listener);

    // TODO: Handle layer changes

    // TODO: This causes certain applications (e.g. Fuzzel) to continuously commit even if there are no changes
    //       We could check and lazily reconfigure only if change, or just ignore the problem. Waybar and rofi both don't exhibit this issue
    //       and even Fuzzel is only visible on screen for a short period of time while selecting

    // TODO: Handle layer keyboard focus preferences

    output_reconfigure(get_output_for_surface(layer_surface));
}

static
void layer_surface_unmap(wl_listener*, void*)
{
    // TODO
}

static
void layer_surface_destroy(wl_listener* listener, void*)
{
    LayerSurface* layer_surface = listener_userdata<LayerSurface*>(listener);

    Output* output = get_output_for_surface(layer_surface);
    if (output) {
        for (zwlr_layer_shell_v1_layer layer : output->layers.enum_values) {
            std::erase(output->layers[layer], layer_surface);
        }
    }

    wlr_scene_node_destroy(&layer_surface->popup_tree->node);

    output_reconfigure(output);

    layer_surface->wlr_surface->data = nullptr;

    delete layer_surface;
}

void server_new_layer_surface(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);

    wlr_layer_surface_v1* wlr_layer_surface = static_cast<wlr_layer_surface_v1*>(data);
    wlr_scene_tree* scene_layer = server->layers[strata_from_wlr(wlr_layer_surface->pending.layer)];

    Output* output = Output::from(wlr_layer_surface->output);
    if (!output) output = get_nearest_output_to_point(server, Point{server->cursor->x, server->cursor->y});
    if (!output) {
        wlr_layer_surface_v1_destroy(wlr_layer_surface);
        return;
    }

    LayerSurface* layer_surface = new LayerSurface{};
    layer_surface->role = SurfaceRole::layer_surface;
    layer_surface->server = server;
    layer_surface->wlr_surface = wlr_layer_surface->surface;
    layer_surface->wlr_surface->data = layer_surface;

    layer_surface->listeners.listen(&wlr_layer_surface->surface->events.commit,  layer_surface, layer_surface_commit);
    layer_surface->listeners.listen(&wlr_layer_surface->surface->events.unmap,   layer_surface, layer_surface_unmap);
    layer_surface->listeners.listen(&         wlr_layer_surface->events.destroy, layer_surface, layer_surface_destroy);

    layer_surface->scene_layer_surface = wlr_scene_layer_surface_v1_create(scene_layer, wlr_layer_surface);
    layer_surface->scene_tree = layer_surface->scene_layer_surface->tree;
    layer_surface->scene_tree->node.data = layer_surface;

    layer_surface->popup_tree = wlr_scene_tree_create(server->layers[Strata::top]);

    output->layers[wlr_layer_surface->pending.layer].emplace_back(layer_surface);

    wlr_surface_send_enter(layer_surface->wlr_surface, output->wlr_output);
    surface_focus(layer_surface);
}

// -----------------------------------------------------------------------------

void popup_commit(wl_listener* listener, void*)
{
    Popup* popup = listener_userdata<Popup*>(listener);

    wlr_xdg_popup* xdg_popup = popup->xdg_popup();

    if (!xdg_popup->base->initial_commit) {
        return;
    }

    Surface* parent = Surface::from(xdg_popup->parent);
    popup->scene_tree = wlr_scene_xdg_surface_create(parent->popup_tree, xdg_popup->base);
    popup->popup_tree = popup->scene_tree;
    popup->scene_tree->node.data = parent;

    Output* output = get_nearest_output_to_point(popup->server, {popup->server->cursor->x, popup->server->cursor->y});
    if (output) {
        wlr_box output_bounds = output_get_bounds(output);

        {
            Surface* cur = parent;
            while (cur->role == SurfaceRole::popup) {
                cur = Surface::from(Popup::from(cur)->xdg_popup()->parent);
            }

            wlr_box coord_system = surface_get_coord_system(cur);
            output_bounds.x -= coord_system.x;
            output_bounds.y -= coord_system.y;
        }

        wlr_xdg_popup_unconstrain_from_box(xdg_popup, &output_bounds);
    } else {
        log_error("No output while opening popup!");
    }
}

void popup_destroy(wl_listener* listener, void*)
{
    Popup* popup = listener_userdata<Popup*>(listener);

    popup->wlr_surface->data = nullptr;

    delete popup;
}

void server_new_popup(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_xdg_popup* xdg_popup = static_cast<wlr_xdg_popup*>(data);

    Popup* popup = new Popup{};
    popup->role = SurfaceRole::popup;
    popup->server = server;
    popup->wlr_surface = xdg_popup->base->surface;
    popup->wlr_surface->data = popup;

    popup->listeners.listen(&xdg_popup->base->surface->events.commit,  popup, popup_commit);
    popup->listeners.listen(&               xdg_popup->events.destroy, popup, popup_destroy);
}
