#include "pch.hpp"
#include "core.hpp"

uint32_t get_modifiers(Server* server)
{
    wlr_keyboard* keyboard = wlr_seat_get_keyboard(server->seat);
    if (!keyboard) return false;
    return wlr_keyboard_get_modifiers(keyboard);
}

bool is_main_mod_down(Server* server)
{
    return get_modifiers(server) & server->main_modifier;
}

bool handle_keybinding(Server* server, xkb_keysym_t sym)
{
    switch (sym)
    {
        case XKB_KEY_Escape:
            wl_display_terminate(server->display);
            break;
        case XKB_KEY_Tab:
        case XKB_KEY_ISO_Left_Tab: {
            bool do_cycle = true;
            if (server->interaction_mode ==  InteractionMode::passthrough) {
                do_cycle = get_focused_toplevel(server);
                focus_cycle_begin(server, nullptr);
            }
            if (do_cycle && server->interaction_mode == InteractionMode::focus_cycle) {
                focus_cycle_step(server, nullptr, sym == XKB_KEY_ISO_Left_Tab);
            }
            break;
        }
        case XKB_KEY_t:
            spawn("konsole", {"konsole"});
            break;
        case XKB_KEY_d:
            spawn("wofi", {"wofi", "--show", "drun"});
            break;
        case XKB_KEY_i:
            spawn("xeyes", {"xeyes"});
            break;
        case XKB_KEY_g:
            spawn("steam", {"steam"});
            break;
        case XKB_KEY_s:
            toplevel_unfocus(get_focused_toplevel(server), false);
            wlr_seat_pointer_clear_focus(server->seat);
            break;
        case XKB_KEY_q:
            if (Toplevel* focused = get_focused_toplevel(server)) {
                wlr_xdg_toplevel_send_close(focused->xdg_toplevel());
            }
            break;
        case XKB_KEY_f:
            if (Toplevel* focused = get_focused_toplevel(server)) {
                toplevel_set_fullscreen(focused, !focused->xdg_toplevel()->current.fullscreen);
            }
            break;
        default:
            return false;
    }
    return true;
}

void keyboard_handle_modifiers(wl_listener* listener, void*)
{
    Keyboard* keyboard = listener_userdata<Keyboard*>(listener);

    // NOTE: Wayland only supports one keyboard at a time, so set the most recently used keyboard as the current one
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);

    // Send modifiers to the client
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

void keyboard_handle_key(wl_listener* listener, void* data)
{
    Keyboard* keyboard = listener_userdata<Keyboard*>(listener);
    Server* server = keyboard->server;
    wlr_seat* seat = server->seat;
    wlr_keyboard_key_event* event = static_cast<wlr_keyboard_key_event*>(data);

    // Translate libinput keycode -> xkbcommon
    uint32_t keycode = event->keycode + 8;

    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    bool handled = false;
    for (int i = 0; i < nsyms; ++i) {
        xkb_keysym_t sym = syms[i];

        if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && is_main_mod_down(server)) {
            if ((handled = handle_keybinding(server, sym))) {
                break;
            }
        }

        // TODO: Separate out into centralized input handle callback
        if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED && server->interaction_mode == InteractionMode::focus_cycle) {
            if (sym == server->main_modifier_keysym_left || sym == server->main_modifier_keysym_right) {
                focus_cycle_end(server);
            }
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
    }
}

void keyboard_handle_destroy(wl_listener* listener, void*)
{
    Keyboard* keyboard = listener_userdata<Keyboard*>(listener);

    std::erase(keyboard->server->keyboards, keyboard);
    delete keyboard;

    // TODO: We need to unset wl_seat capabilities if this was the only keyboard
}

void seat_keyboard_focus_change(wl_listener*, void* data)
{
    wlr_seat_keyboard_focus_change_event* event = static_cast<wlr_seat_keyboard_focus_change_event*>(data);

    if (Toplevel* toplevel = Toplevel::from(event->old_surface)) toplevel_update_border(toplevel);
    if (Toplevel* toplevel = Toplevel::from(event->new_surface)) toplevel_update_border(toplevel);
}

void server_new_keyboard(Server* server, wlr_input_device* device)
{
    wlr_keyboard* wlr_keyboard = wlr_keyboard_from_input_device(device);

    Keyboard* keyboard = new Keyboard{};
    keyboard->server = server;
    keyboard->wlr_keyboard = wlr_keyboard;

    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_keymap* keymap = xkb_keymap_new_from_names(context, ptr(xkb_rule_names{
        .layout = keyboard_layout,
    }), XKB_KEYMAP_COMPILE_NO_FLAGS);

    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(wlr_keyboard, keyboard_repeat_rate, keyboard_repeat_delay);

    keyboard->listeners.listen(&wlr_keyboard->events.modifiers, keyboard, keyboard_handle_modifiers);
    keyboard->listeners.listen(&wlr_keyboard->events.key,       keyboard, keyboard_handle_key);
    keyboard->listeners.listen(&      device->events.destroy,   keyboard, keyboard_handle_destroy);

    wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

    server->keyboards.emplace_back(keyboard);
}

void server_new_pointer(Server* server, wlr_input_device* device)
{
    libinput_device* libinput_device;
    if (wlr_input_device_is_libinput(device) && (libinput_device = wlr_libinput_get_device_handle(device))) {
        if (libinput_device_config_accel_is_available(libinput_device)) {
            libinput_device_config_accel_set_profile(libinput_device, LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
            libinput_device_config_accel_set_speed(  libinput_device, libinput_mouse_speed);
        }
    }

    wlr_cursor_attach_input_device(server->cursor, device);
}

void server_new_input(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_input_device* device = static_cast<wlr_input_device*>(data);
    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            server_new_keyboard(server, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            server_new_pointer(server, device);
            break;
        default:
            break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!server->keyboards.empty()) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }

    wlr_seat_set_capabilities(server->seat, caps);
}

void seat_reset_cursor(Server* server)
{
    wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "default");
    server->cursor_visible = true;
}

void seat_request_set_cursor(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_seat_pointer_request_set_cursor_event* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
    wlr_seat_client* focused_client = server->seat->pointer_state.focused_client;

    if (focused_client != event->seat_client) return;

    // A client may only request an empty cursor if they have keyboard focus
    if (!event->surface && server->seat->keyboard_state.focused_client != event->seat_client) {
        log_warn("Client attempted to hide the cursor without keyboard focus, reset to default cursor");
        seat_reset_cursor(server);
        return;
    }

    wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
    server->cursor_visible = bool(event->surface);
}

void seat_pointer_focus_change(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);

    wlr_seat_pointer_focus_change_event* event = static_cast<wlr_seat_pointer_focus_change_event*>(data);
    if (!event->new_surface) {
        seat_reset_cursor(server);
    }
}

void seat_request_set_selection(wl_listener* listener, void* data)
{
    // TODO: Validate serial?

    Server* server = listener_userdata<Server*>(listener);

    wlr_seat_request_set_selection_event* event = static_cast<wlr_seat_request_set_selection_event*>(data);
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

// -----------------------------------------------------------------------------

void seat_request_start_drag(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_seat_request_start_drag_event* event = static_cast<wlr_seat_request_start_drag_event*>(data);

    if (wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial)) {
        wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
    } else {
        wlr_data_source_destroy(event->drag->source);
    }
}

static
void seat_drag_icon_destroy(wl_listener* listener, void*)
{
    Server* server = listener_userdata<Server*>(listener);

    // TODO: Refocus last focused toplevel

    process_cursor_motion(server, 0, nullptr, 0, 0, 0, 0);

    unlisten(listener_from(listener));
}

void seat_start_drag(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_drag* drag = static_cast<wlr_drag*>(data);
    if (!drag->icon) return;

    drag->icon->data = &wlr_scene_drag_icon_create(server->drag_icon_parent, drag->icon)->node;
    listen(&drag->icon->events.destroy, server, seat_drag_icon_destroy);
}

static
void seat_drag_update_position(Server* server)
{
    wlr_scene_node_set_position(&server->drag_icon_parent->node, int(std::round(server->cursor->x)), int(std::round(server->cursor->y)));

    // TODO: This should be on a separate layer that is always on top (above even the OVERLAY layer)
    wlr_scene_node_raise_to_top(&server->drag_icon_parent->node);
}

// -----------------------------------------------------------------------------

void server_pointer_constraint_destroy(wl_listener* listener, void* data)
{
    wlr_pointer_constraint_v1* constraint = static_cast<wlr_pointer_constraint_v1*>(data);
    log_info("destroying pointer constraint: {}", (void*)constraint);

    Server* server = listener_userdata<Server*>(listener);
    if (server->pointer.active_constraint == constraint) {
        log_info("  was active!");
        server->pointer.active_constraint = nullptr;
    }

    wlr_pointer_constraint_v1* new_constraint;
    wl_list_for_each(new_constraint, &server->pointer.pointer_constraints->constraints, link) {
        if (constraint == new_constraint) {
            continue;
        }

        // TODO: Select constraint based on focused window at cursor move time

        log_info("  replacing with next constriant: {}", (void*)new_constraint);

        server->pointer.active_constraint = new_constraint;
        wlr_pointer_constraint_v1_send_activated(new_constraint);
    }

    unlisten(listener_from(listener));
}

void server_pointer_constraint_new(wl_listener* listener, void* data)
{
    wlr_pointer_constraint_v1* constraint = static_cast<wlr_pointer_constraint_v1*>(data);
    log_info("creating pointer constraint: {}", (void*)constraint);

    Server* server = listener_userdata<Server*>(listener);

    if (server->pointer.active_constraint) {
        log_info("  replacing previous constraint: {}", (void*)server->pointer.active_constraint);
        wlr_pointer_constraint_v1_send_deactivated(server->pointer.active_constraint);
    }
    server->pointer.active_constraint = constraint;
    wlr_pointer_constraint_v1_send_activated(constraint);

    listen(&constraint->events.destroy, server, server_pointer_constraint_destroy);
}

// -----------------------------------------------------------------------------

void reset_interaction_state(Server* server)
{
    if (server->interaction_mode == InteractionMode::focus_cycle) {
        focus_cycle_end(server);
    }
    if (server->interaction_mode == InteractionMode::zone) {
        zone_end_selection(server);
    }
    server->interaction_mode = InteractionMode::passthrough;
    server->movesize.grabbed_toplevel = nullptr;
}

void process_cursor_move(Server* server)
{
    Toplevel* toplevel = server->movesize.grabbed_toplevel;
    wlr_scene_node_set_position(&toplevel->scene_tree->node, server->cursor->x - server->movesize.grab_x, server->cursor->y - server->movesize.grab_y);
}

void process_cursor_resize(Server* server)
{
    auto& movesize = server->movesize;

    int border_x = int(server->cursor->x - movesize.grab_x);
    int border_y = int(server->cursor->y - movesize.grab_y);

    int left   = movesize.grab_geobox.x;
    int top    = movesize.grab_geobox.y;
    int right  = movesize.grab_geobox.x + movesize.grab_geobox.width;
    int bottom = movesize.grab_geobox.y + movesize.grab_geobox.height;

    if      (movesize.resize_edges & WLR_EDGE_TOP)    top    = std::min(border_y, bottom - 1);
    else if (movesize.resize_edges & WLR_EDGE_BOTTOM) bottom = std::max(border_y, top    + 1);

    if      (movesize.resize_edges & WLR_EDGE_LEFT)  left  = std::min(border_x, right - 1);
    else if (movesize.resize_edges & WLR_EDGE_RIGHT) right = std::max(border_x, left  + 1);

    toplevel_set_bounds(movesize.grabbed_toplevel, {
        .x = left - movesize.grabbed_toplevel->xdg_toplevel()->base->geometry.x,
        .y = top  - movesize.grabbed_toplevel->xdg_toplevel()->base->geometry.y,
        .width  = right  - left,
        .height = bottom - top,
    });
}

void process_cursor_motion(Server* server, uint32_t time_msecs, wlr_input_device* device, double dx, double dy, double dx_unaccel, double dy_unaccel)
{
    // defer {
    //     wlr_scene_node_set_enabled(&server->pointer.debug_visual->node, true);
    //     wlr_scene_node_set_position(&server->pointer.debug_visual->node, server->cursor->x - 6, server->cursor->y - 6);
    //     wlr_scene_node_raise_to_top(&server->pointer.debug_visual->node);
    // };

    // Handle compositor interactions

    if (server->interaction_mode == InteractionMode::move) {
        wlr_cursor_move(server->cursor, device, dx, dy);
        process_cursor_move(server);
        return;
    } else if (server->interaction_mode == InteractionMode::resize) {
        wlr_cursor_move(server->cursor, device, dx, dy);
        process_cursor_resize(server);
        return;
    } else if (server->interaction_mode == InteractionMode::zone) {
        wlr_cursor_move(server->cursor, device, dx, dy);
        zone_process_cursor_motion(server);
        return;
    }

    // Get focused surface

    double sx = 0, sy = 0;
    wlr_seat* seat = server->seat;
    struct wlr_surface* wlr_surface = nullptr;
    if (Surface* surface; server->interaction_mode == InteractionMode::pressed && (surface = Surface::from(seat->pointer_state.focused_surface))) {
        wlr_surface = surface->wlr_surface;
        wlr_box coord_system = surface_get_coord_system(surface);
        sx = server->cursor->x - coord_system.x;
        sy = server->cursor->y - coord_system.y;
    }

    if (!wlr_surface) {
        // TODO: Create get_surface_at to handle toplevels, popups, layer shells (when implemented)
        get_toplevel_at(server, server->cursor->x, server->cursor->y, &wlr_surface, &sx, &sy);
    }

    // Handle constraints and update mouse

    if (time_msecs && device) {
        wlr_relative_pointer_manager_v1_send_relative_motion(server->pointer.relative_pointer_manager, server->seat, uint64_t(time_msecs) * 1000, dx, dy, dx_unaccel, dy_unaccel);

        if (wlr_pointer_constraint_v1* constraint = server->pointer.active_constraint; constraint && !server->debug.ignore_mouse_constraints) {

            Surface* surface = Surface::from(constraint->surface);
            if (surface == get_focused_toplevel(server)) {

                wlr_box bounds = surface_get_bounds(surface);
                sx = server->cursor->x - bounds.x;
                sy = server->cursor->y - bounds.y;

                double sx_confined, sy_confined;
                if (wlr_region_confine(&constraint->region, sx, sy, sx + dx, sy + dy, &sx_confined, &sy_confined)) {
                    dx = sx_confined - sx;
                    dy = sy_confined - sy;
                }

                if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
                    return;
                }
            }
        }

        wlr_cursor_move(server->cursor, device, dx, dy);
    }

    // Update xcursor

    if (!wlr_surface) {
        seat_reset_cursor(server);
    }

    // Notify

    if (wlr_surface) {
        wlr_seat_pointer_notify_enter(seat, wlr_surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time_msecs, sx, sy);
    } else {
        wlr_seat_pointer_notify_clear_focus(seat);
    }

    // Update drag

    seat_drag_update_position(server);
}

void server_cursor_motion(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_pointer_motion_event* event = static_cast<wlr_pointer_motion_event*>(data);

    // TODO: Handle custom acceleration here

    process_cursor_motion(server, event->time_msec, &event->pointer->base, event->delta_x, event->delta_y, event->unaccel_dx, event->unaccel_dy);
}

void server_cursor_motion_absolute(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_pointer_motion_absolute_event* event = static_cast<wlr_pointer_motion_absolute_event*>(data);

    double lx, ly;
    if (event->pointer->output_name) {
        wlr_output_layout_output* layout_output;
        wl_list_for_each(layout_output, &server->output_layout->outputs, link) {
            if (strcmp(layout_output->output->name, event->pointer->output_name) == 0) {
                lx = layout_output->x + layout_output->output->width * event->x;
                ly = layout_output->y + layout_output->output->height * event->y;
                break;
            }
        }
    } else {
        // TODO: *can* output_name be null?
        wlr_cursor_absolute_to_layout_coords(server->cursor, &event->pointer->base, event->x, event->y, &lx, &ly);
    }

    double dx = lx - server->cursor->x;
    double dy = ly - server->cursor->y;
    process_cursor_motion(server, event->time_msec, &event->pointer->base, dx, dy, dx, dy);
}

void server_cursor_button(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_pointer_button_event* event = static_cast<wlr_pointer_button_event*>(data);

    // TODO: Focus follows mouse?
    // TODO: Move interaction logic to separate source

    // Handle interrupt focus cycle

    bool focus_cycle_interrupted = event->state == WL_POINTER_BUTTON_STATE_PRESSED && server->interaction_mode == InteractionMode::focus_cycle;
    if (focus_cycle_interrupted) {
        focus_cycle_end(server);
        focus_cycle_interrupted = true;
    }

    double sx, sy;
    wlr_surface* surface = nullptr;
    Toplevel* toplevel_under_cursor = get_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    if (focus_cycle_interrupted && toplevel_under_cursor != get_focused_toplevel(server)) {
        // If we interrupted a focus cycle by clicking outside of the now focused window, drop focus
        // as it wouldn't have been visible to the user before the press
        toplevel_unfocus(get_focused_toplevel(server), false);
        return;
    }

    // Zone interaction

    if (server->interaction_mode == InteractionMode::passthrough || server->interaction_mode == InteractionMode::zone) {
        if (zone_process_cursor_button(server, event)) return;
    }

    // Leave move/size/pressed modes on release

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        // TODO: Do we want this reset cursor_mode from `pressed` if *any* button is released?
        reset_interaction_state(server);
        wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
        return;
    }

    // Focus window on any button press

    if (toplevel_under_cursor) {
        toplevel_focus(toplevel_under_cursor);
    } else {
        log_warn("Unfocusing window");
        toplevel_unfocus(get_focused_toplevel(server), false);
    }

    server->interaction_mode = InteractionMode::pressed;

    // Check for move/size interaction begin

    if (toplevel_under_cursor && is_main_mod_down(server)) {
        if (event->button == BTN_LEFT && (get_modifiers(server) & WLR_MODIFIER_SHIFT)) {
            if (server->cursor_visible) {
                toplevel_begin_interactive(toplevel_under_cursor, InteractionMode::move, 0);
            } else {
                log_warn("Tried to initiate move but cursor not visible");
            }
            return;
        } else if (event->button == BTN_RIGHT) {
            wlr_box bounds = surface_get_bounds(toplevel_under_cursor);
            int nine_slice_x = ((server->cursor->x - bounds.x) * 3) / bounds.width;
            int nine_slice_y = ((server->cursor->y - bounds.y) * 3) / bounds.height;

            InteractionMode type = InteractionMode::resize;
            uint32_t edges = 0;

            if      (nine_slice_x < 1) edges |= WLR_EDGE_LEFT;
            else if (nine_slice_x > 1) edges |= WLR_EDGE_RIGHT;

            if      (nine_slice_y < 1) edges |= WLR_EDGE_TOP;
            else if (nine_slice_y > 1) edges |= WLR_EDGE_BOTTOM;

            // If no edges selected, must be center - switch to move
            if (!edges) type = InteractionMode::move;

            if (server->cursor_visible) {
                toplevel_begin_interactive(toplevel_under_cursor, type, edges);
            } else {
                log_warn("Tried to initiate resize but cursor not visible");
            }
            return;
        } else if (event->button == BTN_MIDDLE) {
            if (server->cursor_visible) {
                wlr_xdg_toplevel_send_close(toplevel_under_cursor->xdg_toplevel());
            } else {
                log_warn("Tried to close-under-cursor but cursor not visible");
            }
            return;
        }
    }

    // ... else passthrough to client

    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

void server_cursor_axis(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_pointer_axis_event* event = static_cast<wlr_pointer_axis_event*>(data);

    if (is_main_mod_down(server) && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        if (server->cursor_visible) {
            if (server->interaction_mode ==  InteractionMode::passthrough) {
                focus_cycle_begin(server, server->cursor);
            }
            if (server->interaction_mode == InteractionMode::focus_cycle) {
                focus_cycle_step(server, server->cursor, event->delta > 0);
            }
        } else {
            log_warn("Tried to focus scroll but cursor not visible");
        }

        return;
    }


    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta, event->delta_discrete, event->source, event->relative_direction);
}

void server_cursor_frame(wl_listener* listener, void*)
{
    Server* server = listener_userdata<Server*>(listener);

    wlr_seat_pointer_notify_frame(server->seat);
}
