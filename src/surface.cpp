#include "pch.hpp"
#include "core.hpp"

void toplevel_close(Toplevel* toplevel)
{
    wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel());
}

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

    wlr_box geom = surface_get_geometry(toplevel);

    bool show = geom.width && geom.height;
    show &= !toplevel_is_fullscreen(toplevel);

    wlr_box positions[4];
    positions[left]   = { -border_width, -border_width, border_width, geom.height + border_width * 2 };
    positions[right]  = {  geom.width,   -border_width, border_width, geom.height + border_width * 2 };
    positions[top]    = {  0,            -border_width, geom.width, border_width                     };
    positions[bottom] = {  0,             geom.height,  geom.width, border_width                     };

    for (uint32_t i = 0; i < 4; ++i) {
        if (show) {
            wlr_scene_node_set_enabled(&toplevel->border[i]->node, true);
            wlr_scene_node_set_position(&toplevel->border[i]->node, positions[i].x, positions[i].y);
            wlr_scene_rect_set_size(toplevel->border[i], positions[i].width, positions[i].height);
            wlr_scene_rect_set_color(toplevel->border[i],
                get_focused_surface(toplevel->server) == toplevel
                    ? glm::value_ptr(border_color_focused)
                    : glm::value_ptr(border_color_unfocused));
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

        // wlroots does not seem to like geometry origins that falls outside of the surface
        // TODO: This *seems* like a bug in wlroots scene system, but...
        // TODO: This also may be dependent on subsurface bounds
        // NOTE: This also *incidentally* works around a bug in GLFW where GLFW requests
        //        additional space around the requested surface size...
        geom.x = std::max(0, geom.x);
        geom.y = std::max(0, geom.y);

        // Some clients report geometry that is larger than the surface size, clamp
        geom.width  = std::clamp(surface->wlr_surface->current.width  - geom.x, 0, geom.width);
        geom.height = std::clamp(surface->wlr_surface->current.height - geom.y, 0, geom.height);

        // Some clients fail to report valid geometry. Fall back to surface dimensions.
        if (geom.width <= 0)  geom.width  = std::max(0, xdg_surface->surface->current.width  - geom.x);
        if (geom.height <= 0) geom.height = std::max(0, xdg_surface->surface->current.height - geom.y);

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

static
void toplevel_resize(Toplevel* toplevel, int width, int height, bool force)
{
    if (toplevel->resize.enable_throttle_resize && toplevel->resize.last_resize_serial > toplevel->resize.last_commited_serial) {
        if (!toplevel->resize.any_pending || width != toplevel->resize.pending_width || height != toplevel->resize.pending_height) {
            toplevel->resize.any_pending = true;
            toplevel->resize.pending_width = width;
            toplevel->resize.pending_height = height;
        }
    } else {
        toplevel->resize.any_pending = false;

        if (force || toplevel->xdg_toplevel()->pending.width != width || toplevel->xdg_toplevel()->pending.height != height) {
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
    process_cursor_motion(toplevel->server, 0, nullptr, {}, {}, {});

    if (toplevel->resize.any_pending) {
        toplevel->resize.any_pending = false;
        toplevel_resize(toplevel, toplevel->resize.pending_width, toplevel->resize.pending_height, false);
    }
}

static
void toplevel_update_position_for_anchor(Toplevel* toplevel)
{
    wlr_box geom = surface_get_geometry(toplevel);
    int x = (toplevel->anchor_edges & WLR_EDGE_RIGHT)  ? toplevel->anchor.x - geom.width  : toplevel->anchor.x;
    int y = (toplevel->anchor_edges & WLR_EDGE_BOTTOM) ? toplevel->anchor.y - geom.height : toplevel->anchor.y;
    wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
}

void toplevel_set_bounds(Toplevel* toplevel, wlr_box box, wlr_edges locked_edges)
{
    if (toplevel->xdg_toplevel()->current.maximized) {
        wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel(), false);
    }

    // NOTE: Bounds are set with parent node relative positions, unlike get_bounds which returns layout relative positions
    //       Thus you must be careful when setting/getting bounds with positioned parents
    // TODO: Tidy up this API and make it clear what is relative to what.

    toplevel->anchor_edges = wlr_edges(locked_edges);
    toplevel->anchor.x = (locked_edges & WLR_EDGE_RIGHT)  ? box.x + box.width  : box.x;
    toplevel->anchor.y = (locked_edges & WLR_EDGE_BOTTOM) ? box.y + box.height : box.y;

    toplevel_update_position_for_anchor(toplevel);
    toplevel_resize(toplevel, box.width, box.height, false);
}

bool toplevel_is_fullscreen(Toplevel* toplevel)
{
    return toplevel->xdg_toplevel()->current.fullscreen;
}

void toplevel_set_fullscreen(Toplevel* toplevel, bool fullscreen, Output* output)
{
    if (fullscreen) {
        wlr_box prev = surface_get_bounds(toplevel);
        if (!output) output = get_output_for_surface(toplevel);
        if (output) {
            wlr_box b = output_get_bounds(output);
            wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel(), true);
            toplevel_set_bounds(toplevel, b);
            toplevel->prev_bounds = prev;
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
    log_info("{} toplevel: {}", active ? "Activating" : "Dectivating", surface_to_string(toplevel));
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel(), active);
    toplevel_update_border(toplevel);
}

void request_activate(wl_listener*, void* data)
{
    wlr_xdg_activation_v1_request_activate_event* event = static_cast<wlr_xdg_activation_v1_request_activate_event*>(data);

    if (Toplevel* toplevel = Toplevel::from(event->surface)) {
        log_debug("Activation request for {}, activating...", surface_to_string(toplevel));
        surface_focus(toplevel);
    }
}

// -----------------------------------------------------------------------------

static
void walk_toplevels(Server* server, bool(*for_each)(void*, Toplevel*), void* for_each_data, bool backward)
{
    auto fn = [&](wlr_scene_node* node, ivec2) -> bool {
        if (Toplevel* toplevel = Toplevel::from(node)) {
            if (&toplevel->scene_tree->node != node) {
                // TODO: Root cause this issue in wlroots
                log_error("BUG - Unexpected wlr_scene_node referencing {} (expected {}, got {}), unlinking!",
                    surface_to_string(toplevel),
                    (void*)&toplevel->scene_tree->node,
                    (void*)node);
                node->data = nullptr;
                return true;
            }
            if (!toplevel->xdg_toplevel()->base->initialized) return true;
            return for_each(for_each_data, toplevel);
        }
        return true;
    };
    if (backward) {
        walk_scene_tree_back_to_front(&server->scene->tree.node, {}, FUNC_REF(fn), false);
    } else {
        walk_scene_tree_front_to_back(&server->scene->tree.node, {}, FUNC_REF(fn), false);
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

    surface_unfocus(get_focused_surface(server));

    Toplevel* current = nullptr;
    auto fn = [&](Toplevel* toplevel) -> bool {
        bool new_current = !current && (!cursor || wlr_box_contains_point(ptr(surface_get_bounds(toplevel)), cursor->x, cursor->y));
        focus_cycle_toplevel_set_enabled(toplevel, new_current);
        if (new_current) current = toplevel;
        return true;
    };
    walk_toplevels_front_to_back(server, FUNC_REF(fn));
}

Toplevel* focus_cycle_end(Server* server)
{
    if (server->interaction_mode != InteractionMode::focus_cycle) return nullptr;
    server->interaction_mode = InteractionMode::passthrough;

    Toplevel* selected = nullptr;
    auto fn = [&](Toplevel* toplevel) -> bool {
        if (focus_cycle_toplevel_is_enabled(toplevel)) {
            if (!selected) selected = toplevel;
        }

        focus_cycle_toplevel_set_enabled(toplevel, true);
        return true;
    };
    walk_toplevels_front_to_back(server, FUNC_REF(fn));

    return selected;
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

    if (prev_wlr_surface == wlr_surface) return;

    if (Toplevel* prev_toplevel = Toplevel::from(prev_wlr_surface)) {
        surface_unfocus(prev_toplevel);
    }

    log_info("Focusing surface:   {}", surface_to_string(surface));

    Toplevel* toplevel = Toplevel::from(surface);

    wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    if (toplevel) {
        wlr_scene_node_reparent(&surface->scene_tree->node, surface->server->layers[Strata::focused]);
        toplevel_set_activated(Toplevel::from(surface), true);
    }

    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, wlr_surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }

    // TODO: Tidy up and consolidate API for handling (re)focus
    process_cursor_motion(server, 0, nullptr, {}, {}, {});
    update_cursor_state(server);
}

void surface_unfocus(Surface* surface)
{
    if (!surface) return;
    if (get_focused_surface(surface->server) != surface)
        return;

    log_info("Unfocusing surface: {}", surface_to_string(surface));

    // Move to top of "unfocused" layer

    if (Toplevel* toplevel = Toplevel::from(surface)) {
        wlr_scene_node_reparent(&surface->scene_tree->node, surface->server->layers[Strata::floating]);
        wlr_scene_node_raise_to_top(&surface->scene_tree->node);
        toplevel_set_activated(toplevel, false);
    }

    wlr_seat_keyboard_clear_focus(surface->server->seat);

    process_cursor_motion(surface->server, 0, nullptr, {}, {}, {});
    update_cursor_state(surface->server);
}

Surface* get_surface_accepting_input_at(Server* server, vec2 layout_pos, wlr_surface** p_surface, vec2* surface_pos)
{
    Surface* surface = nullptr;
    auto fn = [&](wlr_scene_node* node, ivec2 node_pos) {
        if (node->type != WLR_SCENE_NODE_BUFFER) return true;

        wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
        wlr_scene_surface* scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
        if (!scene_surface) return true;
        wlr_box box = { node_pos.x, node_pos.y, scene_buffer->dst_width, scene_buffer->dst_height };
        if (!wlr_box_contains_point(&box, layout_pos.x, layout_pos.y)) return true;

        *p_surface = scene_surface->surface;
        *surface_pos = layout_pos - vec2(node_pos);

        if (scene_buffer->point_accepts_input && !scene_buffer->point_accepts_input(scene_buffer, &surface_pos->x, &surface_pos->y)) {
            return true;
        }

        wlr_scene_tree* tree = node->parent;
        while (tree && !(surface = Surface::from(&tree->node))) {
            tree = tree->node.parent;
        }

        return !tree;
    };
    walk_scene_tree_front_to_back(&server->scene->tree.node, {}, FUNC_REF(fn), true);

    return surface;
}

void surface_cleanup(Surface* surface)
{
    surface->wlr_surface->data = nullptr;
}

// -----------------------------------------------------------------------------

static
void toplevel_foreign_request_maximize(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);
    log_warn("Foreign activation request for: {}", surface_to_string(toplevel));
    surface_focus(toplevel);
}

void toplevel_map(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    log_debug("Toplevel mapped:    {}", surface_to_string(toplevel));

    // wlr foreign manager
    toplevel->foreign_handle = wlr_foreign_toplevel_handle_v1_create(toplevel->server->foreign_toplevel_manager);
    if (toplevel->xdg_toplevel()->app_id) wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->foreign_handle, toplevel->xdg_toplevel()->app_id);
    if (toplevel->xdg_toplevel()->title) wlr_foreign_toplevel_handle_v1_set_title(toplevel->foreign_handle, toplevel->xdg_toplevel()->title);
    toplevel->foreign_listeners.listen(&toplevel->foreign_handle->events.request_activate, toplevel, toplevel_foreign_request_maximize);

    // xdg foreign
    wlr_xdg_foreign_exported_init(&toplevel->foreign_exported, toplevel->server->foreign_registry);

    surface_focus(toplevel);
}

void toplevel_unmap(wl_listener* listener, void*)
{
    Toplevel* unmapped_toplevel = listener_userdata<Toplevel*>(listener);

    log_debug("Toplevel unmapped:  {}", surface_to_string(unmapped_toplevel));

    // Reset interaction mode if grabbed toplevel was unmapped
    if (unmapped_toplevel == unmapped_toplevel->server->movesize.grabbed_toplevel.get()) {
        set_interaction_mode(unmapped_toplevel->server, InteractionMode::passthrough);
    }

    // TODO: Handle toplevel unmap during zone operation

    if (get_focused_surface(unmapped_toplevel->server) == unmapped_toplevel) {
        surface_unfocus(unmapped_toplevel);

        auto fn = [&](Toplevel* toplevel) {
            if (toplevel != unmapped_toplevel) {
                surface_focus(toplevel);
                return false;
            }
            return true;
        };
        walk_toplevels_front_to_back(unmapped_toplevel->server, FUNC_REF(fn));
    }

    if (unmapped_toplevel->foreign_handle) {
        unmapped_toplevel->foreign_listeners.clear();
        wlr_foreign_toplevel_handle_v1_destroy(unmapped_toplevel->foreign_handle);
        unmapped_toplevel->foreign_handle = nullptr;
    }

    wlr_xdg_foreign_exported_finish(&unmapped_toplevel->foreign_exported);
}

void toplevel_commit(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    if (toplevel->xdg_toplevel()->base->initial_commit) {

        log_info("Toplevel committed: {}", surface_to_string(toplevel));

        for (const WindowRule& rule : window_rules) {
            if (rule.app_id && (!toplevel->xdg_toplevel()->app_id || !std::string_view(toplevel->xdg_toplevel()->app_id).starts_with(rule.app_id))) continue;
            if (rule.title && (!toplevel->xdg_toplevel()->title || !std::string_view(toplevel->xdg_toplevel()->title).starts_with(rule.title))) continue;

            log_warn("  Applying quirks to window");
            toplevel->quirks = rule.quirks;
        }

        decoration_set_mode(toplevel);
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel(), 0, 0);
    } else {
        if (toplevel->xdg_toplevel()->current.width == 0) {
            // Toplevel initial commit response

            wlr_box bounds = surface_get_bounds(toplevel);

            if (toplevel->xdg_toplevel()->parent) {
                // Child, position at center of parent.
                wlr_box parent_bounds = surface_get_bounds(Surface::from(toplevel->xdg_toplevel()->parent->base->surface));
                bounds.x = parent_bounds.x + (parent_bounds.width  - bounds.width)  / 2;
                bounds.y = parent_bounds.y + (parent_bounds.height - bounds.height) / 2;
            } else {
                // Non-child, spawn under mouse
                bounds.x = get_cursor_pos(toplevel->server).x - bounds.width  / 2.0;
                bounds.y = get_cursor_pos(toplevel->server).y - bounds.height / 2.0;
            }

            // Constrain to output (respecting external padding)
            Output* output = get_nearest_output_to_box(toplevel->server, bounds);
            if (output) {
                bounds = constrain_box(bounds, zone_apply_external_padding(output->workarea));
            }

            // Update toplevel bounds
            toplevel_set_bounds(toplevel, bounds);
        }

        if (wlr_box geom = surface_get_geometry(toplevel); !geom.width || !geom.height) {
            log_error("Invalid geometry ({}, {}) ({}, {}) committed by {}", geom.x, geom.y, geom.width, geom.height, surface_to_string(toplevel));
        }

        toplevel_resize_handle_commit(toplevel);
        toplevel_update_position_for_anchor(toplevel);
        toplevel_update_border(toplevel);
    }

    surface_profiler_report_commit(toplevel);
}

void toplevel_destroy(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    log_debug("Toplevel destroyed: {} (wlr_surface = {}, xdg_toplevel = {}, scene_tree.node = {})",
        surface_to_string(toplevel),
        (void*)toplevel->xdg_toplevel()->base->surface,
        (void*)toplevel->xdg_toplevel(),
        (void*)&toplevel->scene_tree->node);

    surface_cleanup(toplevel);

    delete toplevel;
}

bool toplevel_is_interactable(Toplevel* toplevel)
{
    if (toplevel_is_fullscreen(toplevel)) return false;

    return true;
}

void toplevel_begin_interactive(Toplevel* toplevel, InteractionMode mode)
{
    if (!toplevel_is_interactable(toplevel)) return;

    Server* server = toplevel->server;

    uint32_t edges = 0;
    if (mode == InteractionMode::resize) {
        wlr_box bounds = surface_get_bounds(toplevel);
        int nine_slice_x = ((get_cursor_pos(server).x - bounds.x) * 3) / bounds.width;
        int nine_slice_y = ((get_cursor_pos(server).y - bounds.y) * 3) / bounds.height;

        if      (nine_slice_x < 1) edges |= WLR_EDGE_LEFT;
        else if (nine_slice_x > 1) edges |= WLR_EDGE_RIGHT;

        if      (nine_slice_y < 1) edges |= WLR_EDGE_TOP;
        else if (nine_slice_y > 1) edges |= WLR_EDGE_BOTTOM;

        // If no edges selected, must be center - switch to move
        if (!edges) mode = InteractionMode::move;
    }

    server->movesize.grabbed_toplevel = weak_from(toplevel);
    set_interaction_mode(server, mode);

    if (mode == InteractionMode::move) {
        server->movesize.grab = get_cursor_pos(server);
        server->movesize.grab_bounds = surface_get_bounds(toplevel);
    } else {
        server->movesize.grab = get_cursor_pos(server);
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
        if (toplevel->xdg_toplevel()->requested.fullscreen) {
            log_debug("Toplevel {} requested fullscreen on {}", surface_to_string(toplevel), output_to_string(Output::from(toplevel->xdg_toplevel()->requested.fullscreen_output)));
        }
        toplevel_set_fullscreen(toplevel, toplevel->xdg_toplevel()->requested.fullscreen, nullptr);
    }
}

void toplevel_new(wl_listener* listener, void* data)
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

    log_debug("Toplevel created:   {} (wlr_surface = {}, xdg_toplevel = {}, scene_tree.node = {})\n{}  for {}",
        surface_to_string(toplevel),
        (void*)xdg_toplevel->base->surface,
        (void*)xdg_toplevel,
        (void*)&toplevel->scene_tree->node,
        log_indent, client_to_string(Client::from(server, xdg_toplevel->base->client->client)));

    toplevel->popup_tree = toplevel->scene_tree;

    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.map,    toplevel, toplevel_map);
    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.unmap,  toplevel, toplevel_unmap);
    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.commit, toplevel, toplevel_commit);

    toplevel->listeners.listen(&xdg_toplevel->events.destroy,            toplevel, toplevel_destroy);
    toplevel->listeners.listen(&xdg_toplevel->events.request_maximize,   toplevel, toplevel_request_maximize);
    toplevel->listeners.listen(&xdg_toplevel->events.request_minimize,   toplevel, toplevel_request_minimize);
    toplevel->listeners.listen(&xdg_toplevel->events.request_fullscreen, toplevel, toplevel_request_fullscreen);

    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.new_subsurface, server, subsurface_new);

    for (int i = 0; i < 4; ++i) {
        toplevel->border[i] = wlr_scene_rect_create(toplevel->scene_tree, 0, 0, glm::value_ptr(border_color_unfocused));
    }
}

// -----------------------------------------------------------------------------

void subsurface_new(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_subsurface* surface = static_cast<wlr_subsurface*>(data);

    Subsurface* subsurface = new Subsurface{};
    subsurface->role = SurfaceRole::subsurface;
    subsurface->server = server;
    subsurface->wlr_surface = surface->surface;
    subsurface->wlr_surface->data = subsurface;

    log_debug("Subsurface created: {}", surface_to_string(subsurface));

    subsurface->listeners.listen(&surface->surface->events.new_subsurface, server,     subsurface_new);
    subsurface->listeners.listen(&surface->surface->events.commit,         subsurface, subsurface_commit);
    subsurface->listeners.listen(&         surface->events.destroy,        subsurface, subsurface_destroy);
}

void subsurface_commit(wl_listener* listener, void*)
{
    Subsurface* subsurface = listener_userdata<Subsurface*>(listener);

    surface_profiler_report_commit(subsurface);
}

void subsurface_destroy(wl_listener* listener, void*)
{
    Subsurface* subsurface = listener_userdata<Subsurface*>(listener);

    log_debug("Subsurface destroyed: {}", surface_to_string(subsurface));

    surface_cleanup(subsurface);

    delete subsurface;
}

// -----------------------------------------------------------------------------

void decoration_set_mode(Toplevel* toplevel)
{
    if (!toplevel->decoration.xdg_decoration) return;

    if (toplevel->xdg_toplevel()->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration.xdg_decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
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

void layer_surface_unmap(wl_listener*, void*)
{
    // TODO
}

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
    surface_cleanup(layer_surface);

    output_reconfigure(output);

    delete layer_surface;
}

void layer_surface_new(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);

    wlr_layer_surface_v1* wlr_layer_surface = static_cast<wlr_layer_surface_v1*>(data);
    wlr_scene_tree* scene_layer = server->layers[strata_from_wlr(wlr_layer_surface->pending.layer)];

    Output* output = Output::from(wlr_layer_surface->output);
    if (!output) output = get_nearest_output_to_point(server, get_cursor_pos(server));
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

    Output* output = get_nearest_output_to_point(popup->server, get_cursor_pos(popup->server));
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

    surface_cleanup(popup);

    delete popup;
}

void popup_new(wl_listener* listener, void* data)
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
