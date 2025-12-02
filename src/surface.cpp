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

void surface_update_scale(Surface* surface)
{
    float scale = 0.f;

    wlr_surface_output* surface_output;
    wl_list_for_each(surface_output, &surface->wlr_surface->current_outputs, link) {
        scale = std::max(scale, surface_output->output->scale);
    }

    if (!scale) scale = 1.f;

    if (scale != surface->last_scale) {
        surface->last_scale = scale;
        log_debug("Setting preferred scale ({:.2f}) for: {}", scale, surface_to_string(surface));
        wlr_fractional_scale_v1_notify_scale(surface->wlr_surface, scale);
        wlr_surface_set_preferred_buffer_scale(surface->wlr_surface, int32_t(std::ceil(scale)));
    }
}

float toplevel_get_opacity(Toplevel* toplevel)
{
    return (toplevel->server->interaction_mode != InteractionMode::focus_cycle
            || toplevel == toplevel->server->focus_cycle.current.get())
        ? 1
        : toplevel->server->config.layout.focus_cycle_unselected_opacity;
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
        geom.width = surface->wlr_surface->current.width;
        geom.height = surface->wlr_surface->current.height;
    }

    return geom;
}

wlr_box surface_get_coord_system(Surface* surface)
{
    wlr_box box = {};

    wlr_box bounds = surface_get_bounds(surface);
    box.x = bounds.x;
    box.y = bounds.y;

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

    if (Toplevel* toplevel  = Toplevel::from(surface)) {
        wlr_box geom = surface_get_geometry(toplevel);
        box.x = (toplevel->anchor_edges & WLR_EDGE_RIGHT)  ? toplevel->anchor.x - geom.width  : toplevel->anchor.x;
        box.y = (toplevel->anchor_edges & WLR_EDGE_BOTTOM) ? toplevel->anchor.y - geom.height : toplevel->anchor.y;
    } else if (Popup* popup = Popup::from(surface)) {
        auto parent_bounds = surface_get_bounds(Surface::from(popup->xdg_popup()->parent));
        auto xdg_geom = popup->xdg_popup()->current.geometry;
        box.x = parent_bounds.x + xdg_geom.x;
        box.y = parent_bounds.y + xdg_geom.y;
    } else if (LayerSurface* layer_surface = LayerSurface::from(surface)) {
        box.x = layer_surface->position.x;
        box.y = layer_surface->position.y;
    } else if (Subsurface* subsurface = Subsurface::from(surface)) {
        Surface* parent = subsurface->parent();
        auto parent_bounds = surface_get_bounds(parent);
        auto parent_geom = surface_get_geometry(parent);
        auto x = subsurface->subsurface()->current.x;
        auto y = subsurface->subsurface()->current.x;
        box.x = parent_bounds.x - parent_geom.x + x;
        box.y = parent_bounds.y - parent_geom.y + y;
    } else {
        log_error("Get position not implemented for surface: {}", surface_to_string(surface));
    }

    return box;
}

Surface* surface_get_parent(Surface* surface)
{
    if (Subsurface* subsurface = Subsurface::from(surface)) {
        return subsurface->parent();
    } else if (Popup* popup = Popup::from(surface)) {
        return popup->parent();
    }

    return nullptr;
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

    surface_update_scale(toplevel);
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
        if (Output* prev_output = get_nearest_output_to_box(toplevel->server, toplevel->prev_bounds)) {
            wlr_box output_bounds = zone_apply_external_padding(toplevel->server, prev_output->workarea);
            toplevel->prev_bounds = constrain_box(toplevel->prev_bounds, output_bounds);
        }
        toplevel_set_bounds(toplevel, toplevel->prev_bounds);
    }
}

void toplevel_set_activated(Toplevel* toplevel, bool active)
{
    if (!toplevel->xdg_toplevel()->base->initialized) return;

    log_info("{} toplevel: {}", active ? "Activating" : "Dectivating", surface_to_string(toplevel));
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel(), active);
}

void request_activate(wl_listener*, void* data)
{
    wlr_xdg_activation_v1_request_activate_event* event = static_cast<wlr_xdg_activation_v1_request_activate_event*>(data);

    if (Toplevel* toplevel = Toplevel::from(event->surface)) {
        log_debug("Activation request for {}, activating...", surface_to_string(toplevel));
        surface_try_focus(toplevel->server, toplevel);
    }
}

// -----------------------------------------------------------------------------

static
bool focus_cycle_toplevel_in_cycle(Toplevel* toplevel, wlr_cursor* cursor)
{
    return surface_is_mapped(toplevel)
        && (!cursor || wlr_box_contains_point(ptr(surface_get_bounds(toplevel)), cursor->x, cursor->y));
}

void focus_cycle_begin(Server* server, wlr_cursor* cursor)
{
    set_interaction_mode(server, InteractionMode::focus_cycle);

    server->focus_cycle.current.reset();

    for (Toplevel* toplevel : iterate<Toplevel*>(server->toplevels, true)) {
        if (focus_cycle_toplevel_in_cycle(toplevel, cursor)) {
            toplevel->server->focus_cycle.current = weak_from(toplevel);
            break;
        }
    }
}

Toplevel* focus_cycle_end(Server* server)
{
    if (server->interaction_mode != InteractionMode::focus_cycle) return nullptr;
    server->interaction_mode = InteractionMode::passthrough;

    Toplevel* selected = server->focus_cycle.current.get();
    server->focus_cycle.current.reset();

    return selected;
}

void focus_cycle_step(Server* server, wlr_cursor* cursor, bool backwards)
{
    // TODO: Fixup visibility if focus changes mid-cycle (e.g. window added/destroyed)

    Toplevel* first = nullptr;
    bool next_is_active = false;
    Toplevel* new_active = nullptr;

    for (Toplevel* toplevel : iterate<Toplevel*>(server->toplevels, !backwards)) {
        if (!focus_cycle_toplevel_in_cycle(toplevel, cursor)) continue;

        // Is this is the first window in the cycle, mark incase we need to
        if (!first) first = toplevel;

        // Previous window was active, moving to this one
        if (next_is_active) {
            new_active = toplevel;
            break;
        }

        // This is the first active window in the cycle, cycle to next
        if (toplevel->server->focus_cycle.current.get() == toplevel) {
            next_is_active = true;
        }
    };

    if (!new_active && first) {
        new_active = first;
    }

    server->focus_cycle.current = weak_from(new_active);
}

// -----------------------------------------------------------------------------

static
void raise_toplevel(Toplevel* toplevel)
{
    log_debug("Raising to top: {}", surface_to_string(toplevel));
    std::erase(toplevel->server->toplevels, toplevel);
    toplevel->server->toplevels.emplace_back(toplevel);
}

bool surface_is_mapped(Surface* surface)
{
    return surface && surface->wlr_surface->mapped;
}

bool surface_accepts_focus(Surface* surface)
{
    if (!surface_is_mapped(surface)) return false;
    if (LayerSurface* layer_surface = LayerSurface::from(surface))
        return layer_surface->wlr_layer_surface()->current.keyboard_interactive != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
    return true;
}

static
void surface_impl_set_focus(Server* server, Surface* surface)
{
    // This function implement the actual focus switching.
    // Use with caution as it does not respect map status, keyboard interactivity, or exclusivity

    Surface* prev = get_focused_surface(server);

    if (surface == prev) return;

    log_debug("Switching focus from\n    {} to\n    {}", surface_to_string(prev), surface_to_string(surface));

    if (Toplevel* prev_toplevel = Toplevel::from(prev)) {
        toplevel_set_activated(prev_toplevel, false);
    }

    if (Toplevel* new_toplevel = Toplevel::from(surface)) {
        toplevel_set_activated(new_toplevel, true);
        raise_toplevel(new_toplevel);
    }

    // TODO: Where/when should we respect keyboard grabs?

    if (surface) {
        wlr_keyboard* keyboard = wlr_seat_get_keyboard(server->seat);
        wlr_seat_keyboard_enter(server->seat, surface->wlr_surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    } else {
        wlr_seat_keyboard_clear_focus(server->seat);
    }

    process_cursor_motion(server, 0, nullptr, {}, {}, {});
}

static
Surface* find_exclusive_focus(Server* server)
{
    for (auto layer : { ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, ZWLR_LAYER_SHELL_V1_LAYER_TOP }) {
        for (auto* output : server->outputs) {
            for (auto* surface : output->layers[layer]) {
                if (!surface_is_mapped(surface)) continue;
                if (surface->wlr_layer_surface()->current.keyboard_interactive == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
                    return surface;
                }
            }
        }
    }

    return nullptr;
}

static
Surface* find_most_recently_focused_toplevel(Server* server)
{
    for (Toplevel* toplevel : iterate<Toplevel*>(server->toplevels, true)) {
        if (surface_is_mapped(toplevel)) return toplevel;
    }
    return nullptr;
}

void surface_try_focus(Server* server, Surface* surface)
{
    if (!surface_accepts_focus(surface)) {
        surface = nullptr;
    }

    if (Toplevel* toplevel = Toplevel::from(surface)) {
        // Always raise, even if doesn't get focus
        raise_toplevel(toplevel);
    }

    if (Surface* exclusive = find_exclusive_focus(server)) {
        surface = exclusive;
    }

    surface_impl_set_focus(server, surface);
}

void update_focus(Server* server)
{
    Surface* focused = get_focused_surface(server);

    if (focused && !surface_accepts_focus(focused)) {
        focused = find_most_recently_focused_toplevel(server);
    }

    if (Surface* exclusive = find_exclusive_focus(server)) {
        focused = exclusive;
    }

    surface_impl_set_focus(server, focused);
}

Surface* get_surface_accepting_input_at(Server* server, vec2 layout_pos, wlr_surface** p_surface, vec2* surface_pos)
{
    Surface* surface = nullptr;

    if (Surface* s = server->surface_under_cursor.get()) {
        surface = static_cast<Surface*>(server->toplevel_under_cursor.get()) ?: s;

        *p_surface = s->wlr_surface;
        *surface_pos = get_cursor_pos(server) - vec2(s->cached_position);
    } else {
        *p_surface = nullptr;
    }

    return surface;
}

void surface_cleanup(Surface* surface)
{
    surface->wlr_surface->data = nullptr;
}

// -----------------------------------------------------------------------------

static
void toplevel_foreign_request_activate(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);
    log_warn("Foreign activation request for: {}", surface_to_string(toplevel));
    surface_try_focus(toplevel->server, toplevel);
}

void toplevel_map(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    log_debug("Toplevel mapped:    {}", surface_to_string(toplevel));

    // wlr foreign manager
    toplevel->foreign_handle = wlr_foreign_toplevel_handle_v1_create(toplevel->server->foreign_toplevel_manager);
    if (toplevel->xdg_toplevel()->app_id) wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->foreign_handle, toplevel->xdg_toplevel()->app_id);
    if (toplevel->xdg_toplevel()->title) wlr_foreign_toplevel_handle_v1_set_title(toplevel->foreign_handle, toplevel->xdg_toplevel()->title);
    toplevel->foreign_listeners.listen(&toplevel->foreign_handle->events.request_activate, toplevel, toplevel_foreign_request_activate);

    // xdg foreign
    wlr_xdg_foreign_exported_init(&toplevel->foreign_exported, toplevel->server->foreign_registry);

    surface_try_focus(toplevel->server, toplevel);
}

void toplevel_unmap(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);
    Server* server = toplevel->server;

    log_debug("Toplevel unmapped:  {}", surface_to_string(toplevel));

    // Reset interaction mode if grabbed toplevel was unmapped
    if (toplevel == server->movesize.grabbed_toplevel.get()) {
        set_interaction_mode(server, InteractionMode::passthrough);
    }

    if (server->interaction_mode == InteractionMode::focus_cycle && toplevel == server->focus_cycle.current.get()) {
        set_interaction_mode(server, InteractionMode::passthrough);
    }

    update_focus(server);

    if (toplevel->foreign_handle) {
        toplevel->foreign_listeners.clear();
        wlr_foreign_toplevel_handle_v1_destroy(toplevel->foreign_handle);
        toplevel->foreign_handle = nullptr;
    }

    wlr_xdg_foreign_exported_finish(&toplevel->foreign_exported);
}

void toplevel_commit(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    if (toplevel->xdg_toplevel()->base->initial_commit) {

        log_info("Toplevel initial commit: {}", surface_to_string(toplevel));

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
                bounds = constrain_box(bounds, zone_apply_external_padding(toplevel->server, output->workarea));
            }

            // Update toplevel bounds
            toplevel_set_bounds(toplevel, bounds);
        }

        if (wlr_box geom = surface_get_geometry(toplevel); !geom.width || !geom.height) {
            log_error("Invalid geometry ({}, {}) ({}, {}) committed by {}", geom.x, geom.y, geom.width, geom.height, surface_to_string(toplevel));
        }

        toplevel_resize_handle_commit(toplevel);
        surface_update_scale(toplevel);
    }
}

void toplevel_destroy(wl_listener* listener, void*)
{
    Toplevel* toplevel = listener_userdata<Toplevel*>(listener);

    log_debug("Toplevel destroyed: {}", surface_to_string(toplevel));

    std::erase(toplevel->server->toplevels, toplevel);

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
                toplevel_set_bounds(toplevel, zone_apply_external_padding(toplevel->server, output->workarea));
                wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel(), true);
            }
        } else {
            if (Output* output = get_nearest_output_to_box(toplevel->server, toplevel->prev_bounds)) {
                toplevel->prev_bounds = constrain_box(toplevel->prev_bounds, zone_apply_external_padding(toplevel->server, output->workarea));
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

    log_debug("Toplevel created:   {} (wlr_surface = {}, xdg_toplevel = {})\n{}  for {}",
        surface_to_string(toplevel),
        (void*)xdg_toplevel->base->surface,
        (void*)xdg_toplevel,
        log_indent, client_to_string(Client::from(server, xdg_toplevel->base->client->client)));

    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.map,    toplevel, toplevel_map);
    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.unmap,  toplevel, toplevel_unmap);
    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.commit, toplevel, toplevel_commit);

    toplevel->listeners.listen(&xdg_toplevel->events.destroy,            toplevel, toplevel_destroy);
    toplevel->listeners.listen(&xdg_toplevel->events.request_maximize,   toplevel, toplevel_request_maximize);
    toplevel->listeners.listen(&xdg_toplevel->events.request_minimize,   toplevel, toplevel_request_minimize);
    toplevel->listeners.listen(&xdg_toplevel->events.request_fullscreen, toplevel, toplevel_request_fullscreen);

    toplevel->listeners.listen(&xdg_toplevel->base->surface->events.new_subsurface, server, subsurface_new);

    server->toplevels.emplace_back(toplevel);
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

    // log_debug("Subsurface created: {}", surface_to_string(subsurface));

    subsurface->listeners.listen(&surface->surface->events.new_subsurface, server,     subsurface_new);
    subsurface->listeners.listen(&         surface->events.destroy,        subsurface, subsurface_destroy);
}

void subsurface_destroy(wl_listener* listener, void*)
{
    Subsurface* subsurface = listener_userdata<Subsurface*>(listener);

    // log_debug("Subsurface destroyed: {}", surface_to_string(subsurface));

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

static
void layer_surface_exclusive_zone(wlr_layer_surface_v1_state* state, wlr_box* usable_area)
{
	switch (state->anchor) {
        case ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP:
        case ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT:
            // Anchor top
            usable_area->y += state->exclusive_zone + state->margin.top;
            usable_area->height -= state->exclusive_zone + state->margin.top;
            break;
        case ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM:
        case ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT:
            // Anchor bottom
            usable_area->height -= state->exclusive_zone + state->margin.bottom;
            break;
        case ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT:
        case ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT:
            // Anchor left
            usable_area->x += state->exclusive_zone + state->margin.left;
            usable_area->width -= state->exclusive_zone + state->margin.left;
            break;
        case ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT:
        case ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT:
            // Anchor right
            usable_area->width -= state->exclusive_zone + state->margin.right;
            break;
	}

    if (usable_area->width < 0) {
        usable_area->width = 0;
    }

    if (usable_area->height < 0) {
        usable_area->height = 0;
    }
}

static
void layer_surface_configure(LayerSurface* surface, const wlr_box* full_area, wlr_box* usable_area)
{
    wlr_layer_surface_v1* layer_surface = surface->wlr_layer_surface();
    wlr_layer_surface_v1_state* state = &layer_surface->current;

    // If the exclusive zone is set to -1, the layer surface will use the
    // full area of the output, otherwise it is constrained to the
    // remaining usable area.
    wlr_box bounds;
    if (state->exclusive_zone == -1) {
        bounds = *full_area;
    } else {
        bounds = *usable_area;
    }

    wlr_box box = {
        .width = int(state->desired_width),
        .height = int(state->desired_height),
    };

    // Horizontal positioning
    if (box.width == 0) {
        box.x = bounds.x + state->margin.left;
        box.width = bounds.width -
            (state->margin.left + state->margin.right);
    } else if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT &&
            state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) {
        box.x = bounds.x + bounds.width/2 -box.width/2;
    } else if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) {
        box.x = bounds.x + state->margin.left;
    } else if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) {
        box.x = bounds.x + bounds.width - box.width - state->margin.right;
    } else {
        box.x = bounds.x + bounds.width/2 - box.width/2;
    }

    // Vertical positioning
    if (box.height == 0) {
        box.y = bounds.y + state->margin.top;
        box.height = bounds.height -
            (state->margin.top + state->margin.bottom);
    } else if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP &&
            state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) {
        box.y = bounds.y + bounds.height/2 - box.height/2;
    } else if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) {
        box.y = bounds.y + state->margin.top;
    } else if (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) {
        box.y = bounds.y + bounds.height - box.height - state->margin.bottom;
    } else {
        box.y = bounds.y + bounds.height/2 - box.height/2;
    }

    surface->position = {box.x, box.y};
    wlr_layer_surface_v1_configure(layer_surface, box.width, box.height);

    if (layer_surface->surface->mapped && state->exclusive_zone > 0) {
        layer_surface_exclusive_zone(state, usable_area);
    }
}

void output_reconfigure_layer(Output* output, zwlr_layer_shell_v1_layer layer)
{
    wlr_box full_area = output_get_bounds(output);

    for (LayerSurface* layer_surface : output->layers[layer]) {
        if (!layer_surface->wlr_layer_surface()->initialized) continue;

        layer_surface_configure(layer_surface, &full_area, &output->workarea);
    }
}

void layer_surface_commit(wl_listener* listener, void*)
{
    LayerSurface* layer_surface = listener_userdata<LayerSurface*>(listener);

    // TODO: Handle layer changes

    update_focus(layer_surface->server);

    output_reconfigure(get_output_for_surface(layer_surface));

    surface_update_scale(layer_surface);
}

void layer_surface_map(wl_listener* listener, void*)
{
    LayerSurface* layer_surface = listener_userdata<LayerSurface*>(listener);

    surface_try_focus(layer_surface->server, layer_surface);

    layer_surface->server->layer_surfaces.emplace_back(layer_surface);
}

void layer_surface_unmap(wl_listener* listener, void*)
{
    LayerSurface* layer_surface = listener_userdata<LayerSurface*>(listener);

    std::erase(layer_surface->server->layer_surfaces, layer_surface);

    update_focus(layer_surface->server);
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

    surface_cleanup(layer_surface);

    output_reconfigure(output);

    delete layer_surface;
}

void layer_surface_new(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);

    wlr_layer_surface_v1* wlr_layer_surface = static_cast<wlr_layer_surface_v1*>(data);

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
    layer_surface->listeners.listen(&wlr_layer_surface->surface->events.map,     layer_surface, layer_surface_map);
    layer_surface->listeners.listen(&wlr_layer_surface->surface->events.unmap,   layer_surface, layer_surface_unmap);
    layer_surface->listeners.listen(&         wlr_layer_surface->events.destroy, layer_surface, layer_surface_destroy);

    output->layers[wlr_layer_surface->pending.layer].emplace_back(layer_surface);

    wlr_surface_send_enter(layer_surface->wlr_surface, output->wlr_output);
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

        log_info("output bounds ({}, {}), ({}, {})", output_bounds.x, output_bounds.y, output_bounds.width, output_bounds.height);

        wlr_xdg_popup_unconstrain_from_box(xdg_popup, &output_bounds);
        log_info("popup geom ({}, {}) ({}, {})", xdg_popup->scheduled.geometry.x, xdg_popup->scheduled.geometry.y, xdg_popup->scheduled.geometry.width, xdg_popup->scheduled.geometry.height);
    } else {
        log_error("No output while opening popup!");
    }
}

void popup_destroy(wl_listener* listener, void*)
{
    Popup* popup = listener_userdata<Popup*>(listener);

    if (Surface* parent = Surface::from(popup->xdg_popup()->parent)) {
        std::erase(parent->popups, popup);
    }

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

    if (Surface* parent = Surface::from(xdg_popup->parent)) {
        parent->popups.emplace_back(popup);
    }
}
