#include "core.hpp"

#include <libevdev/libevdev.h>

uint32_t get_modifiers(Server* server)
{
    wlr_keyboard* keyboard = wlr_seat_get_keyboard(server->seat);
    if (!keyboard) return false;
    return wlr_keyboard_get_modifiers(keyboard);
}

bool is_main_mod_down(Server* server)
{
    return get_modifiers(server) & server->modifier_key;
}

bool handle_keybinding(Server* server, xkb_keysym_t sym)
{
    switch (sym)
    {
        case XKB_KEY_Escape:
            wl_display_terminate(server->display);
            break;
        case XKB_KEY_Tab:
        case XKB_KEY_ISO_Left_Tab:
            cycle_focus_immediate(server, nullptr, sym == XKB_KEY_ISO_Left_Tab);
            break;
        case XKB_KEY_t:
            spawn("konsole", {"konsole"});
            break;
        case XKB_KEY_d:
            spawn("wofi", {"wofi", "--show", "drun"});
            break;
        case XKB_KEY_q:
            if (server->focused_toplevel && server->focused_toplevel->xdg_toplevel) {
                wlr_xdg_toplevel_send_close(server->focused_toplevel->xdg_toplevel);
            }
            break;
        case XKB_KEY_f:
            if (server->focused_toplevel) {
                bool fullscreen = server->focused_toplevel->xdg_toplevel->current.fullscreen;
                toplevel_set_fullscreen(server->focused_toplevel, !fullscreen);
            }
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

    // Get a list of keysyms based on the keymap for this keyboard
    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    bool handled = false;
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && is_main_mod_down(server)) {
        // If MOD key is held down and this button was pressed, we attempt to process it as a compositor keybinding...

        for (int i = 0; i < nsyms; ++i) {
            if ((handled = handle_keybinding(server, syms[i]))) {
                break;
            }
        }
    }

    if (!handled) {
        // ...otherwise, we pass it along to the client

        wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
    }
}

void keyboard_handle_destroy(wl_listener* listener, void*)
{
    // This event is raised by the keyboard base wlr_input_device to signal the destruction of the wlr_keyboard.
    // It will no longer receieve events and should be destroyed

    Keyboard* keyboard = listener_userdata<Keyboard*>(listener);

    std::erase(keyboard->server->keyboards, keyboard);
    delete keyboard;

    // TODO: We need to unset wl_seat capabilities if this was the only keyboard
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

void seat_request_set_cursor(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_seat_pointer_request_set_cursor_event* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
    wlr_seat_client* focused_client = server->seat->pointer_state.focused_client;

    if (focused_client == event->seat_client) {
        wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

void seat_pointer_focus_change(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);

    wlr_seat_pointer_focus_change_event* event = static_cast<wlr_seat_pointer_focus_change_event*>(data);
    if (!event->new_surface) {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "default");
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

    // Refocus last focused toplevel
    toplevel_focus(server->focused_toplevel);

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

void server_pointer_constraint_destroy(wl_listener* listener, void*)
{
    wlr_log(WLR_INFO, "destroying pointer constraint");

    unlisten(listener_from(listener));
}

void server_pointer_constraint_new(wl_listener*, void* data)
{
    wlr_log(WLR_INFO, "creating pointer constraint");

    wlr_pointer_constraint_v1* constraint = static_cast<wlr_pointer_constraint_v1*>(data);

    listen(&constraint->events.destroy, constraint, server_pointer_constraint_destroy);
}

// -----------------------------------------------------------------------------

void reset_cursor_mode(Server* server)
{
    server->cursor_mode = CursorMode::passthrough;
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
        .x = left - movesize.grabbed_toplevel->xdg_toplevel->base->geometry.x,
        .y = top  - movesize.grabbed_toplevel->xdg_toplevel->base->geometry.y,
        .width  = right  - left,
        .height = bottom - top,
    });
}

void process_cursor_motion(Server* server, uint32_t time_msecs, wlr_input_device *device, double dx, double dy, double dx_unaccel, double dy_unaccel)
{
    if (time_msecs && device) {
        wlr_relative_pointer_manager_v1_send_relative_motion(server->relative_pointer_manager, server->seat, uint64_t(time_msecs) * 1000, dx, dy, dx_unaccel, dy_unaccel);
        wlr_cursor_move(server->cursor, device, dx, dy);
    }


    // Get focused surface

    double sx = 0, sy = 0;
    wlr_seat* seat = server->seat;
    wlr_surface* surface = nullptr;
    if (server->cursor_mode == CursorMode::pressed && seat->pointer_state.focused_surface) {
        if (wlr_xdg_surface* xdg_surface = wlr_xdg_surface_try_from_wlr_surface(seat->pointer_state.focused_surface)) {
            surface = seat->pointer_state.focused_surface;
            Client* client = static_cast<Client*>(xdg_surface->data);
            wlr_box coord_system = client_get_coord_system(client);
            sx = server->cursor->x - coord_system.x;
            sy = server->cursor->y - coord_system.y;
        }
    }

    // Handle compositor interactions

    seat_drag_update_position(server);

    if (server->cursor_mode == CursorMode::move) {
        process_cursor_move(server);
        return;
    } else if (server->cursor_mode == CursorMode::resize) {
        process_cursor_resize(server);
        return;
    } else if (server->cursor_mode == CursorMode::zone) {
        zone_process_cursor_motion(server);
        return;
    }

    // Update xcursor

    if (!surface) {
        Toplevel* toplevel = get_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
        if (!toplevel) {
            wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "default");
        }
    }

    // Notify

    if (surface) {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time_msecs, sx, sy);
    } else {
        wlr_seat_pointer_notify_clear_focus(seat);
    }

    wlr_scene_node_set_position(&server->debug_cursor_visual->node, server->cursor->x - 6, server->cursor->y - 6);
    wlr_scene_node_raise_to_top(&server->debug_cursor_visual->node);
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
        for (Output* output : server->outputs) {
            if (strcmp(output->wlr_output->name, event->pointer->output_name) == 0) {
                wlr_box bounds = output_get_bounds(output);
                lx = bounds.x + bounds.width * event->x;
                ly = bounds.y + bounds.height * event->y;
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

    // Zone interaction

    if (zone_process_cursor_button(server, event)) return;

    // Leave move/size/pressed modes on release

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        // TODO: Do we want this reset cursor_mode from `pressed` if *any* button is released?
        reset_cursor_mode(server);
        wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
        return;
    }

    server->cursor_mode = CursorMode::pressed;

    // Focus window on any button press

    double sx, sy;
    wlr_surface* surface = nullptr;
    Toplevel* toplevel = get_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (toplevel) {
        toplevel_focus(toplevel);
    } else {
        toplevel_unfocus(server->focused_toplevel);
    }

    // Check for move/size interaction begin

    if (toplevel && is_main_mod_down(server)) {
        if (event->button == BTN_LEFT && (get_modifiers(server) & WLR_MODIFIER_SHIFT)) {
            toplevel_begin_interactive(toplevel, CursorMode::move, 0);
            return;
        } else if (event->button == BTN_RIGHT) {
            wlr_box bounds = client_get_bounds(toplevel);
            int nine_slice_x = ((server->cursor->x - bounds.x) * 3) / bounds.width;
            int nine_slice_y = ((server->cursor->y - bounds.y) * 3) / bounds.height;

            CursorMode type = CursorMode::resize;
            uint32_t edges = 0;

            if      (nine_slice_x < 1) edges |= WLR_EDGE_LEFT;
            else if (nine_slice_x > 1) edges |= WLR_EDGE_RIGHT;

            if      (nine_slice_y < 1) edges |= WLR_EDGE_TOP;
            else if (nine_slice_y > 1) edges |= WLR_EDGE_BOTTOM;

            // If no edges selected, must be center - switch to move
            if (!edges) type = CursorMode::move;

            toplevel_begin_interactive(toplevel, type, edges);
            return;
        } else if (event->button == BTN_MIDDLE) {
            wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
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

    if (is_main_mod_down(server)) {
        if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
            cycle_focus_immediate(server, server->cursor, event->delta_discrete > 0);
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
