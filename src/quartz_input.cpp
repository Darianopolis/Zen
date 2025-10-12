#include "quartz.hpp"

#include <libevdev/libevdev.h>

uint32_t qz_get_modifiers(qz_server* server)
{
    wlr_keyboard* keyboard = wlr_seat_get_keyboard(server->seat);
    if (!keyboard) return false;
    return wlr_keyboard_get_modifiers(keyboard);
}

bool qz_is_main_mod_down(qz_server* server)
{
    return qz_get_modifiers(server) & server->modifier_key;
}

bool qz_handle_keybinding(qz_server* server, xkb_keysym_t sym)
{
    switch (sym)
    {
        case XKB_KEY_Escape:
            wl_display_terminate(server->wl_display);
            break;
        case XKB_KEY_Tab: {
            if (server->toplevels.size() < 2) {
                break;
            }
            // TODO: Enter focus cycling mode until MOD key is released,
            //       at which point currently cycled window will be moved to top of focus stack
            qz_toplevel* next_toplevel = server->toplevels.front();
            qz_focus_toplevel(next_toplevel);
            break;
        }
        case XKB_KEY_T:
        case XKB_KEY_t:
            qz_spawn("konsole", {"konsole"});
            break;
        case XKB_KEY_D:
        case XKB_KEY_d:
            qz_spawn("wofi", {"wofi", "--show", "drun"});
            break;
        case XKB_KEY_Q:
        case XKB_KEY_q:
            if (server->focused_toplevel && server->focused_toplevel->xdg_toplevel) {
                wlr_xdg_toplevel_send_close(server->focused_toplevel->xdg_toplevel);
            }
            break;
        default:
            return false;
    }
    return true;
}

void qz_keyboard_handle_modifiers(wl_listener* listener, void*)
{
    qz_keyboard* keyboard = qz_listener_userdata<qz_keyboard*>(listener);

    // NOTE: Wayland only supports one keyboard at a time, so set the most recently used keyboard as the current one
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);

    // Send modifiers to the client
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

void qz_keyboard_handle_key(wl_listener* listener, void* data)
{
    qz_keyboard* keyboard = qz_listener_userdata<qz_keyboard*>(listener);
    qz_server* server = keyboard->server;
    wlr_seat* seat = server->seat;
    wlr_keyboard_key_event* event = static_cast<wlr_keyboard_key_event*>(data);

    // Translate libinput keycode -> xkbcommon
    uint32_t keycode = event->keycode + 8;

    // Get a list of keysyms based on the keymap for this keyboard
    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    bool handled = false;
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && qz_is_main_mod_down(server)) {
        // If MOD key is held down and this button was pressed, we attempt to process it as a compositor keybinding...

        for (int i = 0; i < nsyms; ++i) {
            if ((handled = qz_handle_keybinding(server, syms[i]))) {
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

void qz_keyboard_handle_destroy(wl_listener* listener, void*)
{
    // This event is raised by the keyboard base wlr_input_device to signal the destruction of the wlr_keyboard.
    // It will no longer receieve events and should be destroyed

    qz_keyboard* keyboard = qz_listener_userdata<qz_keyboard*>(listener);

    std::erase(keyboard->server->keyboards, keyboard);
    delete keyboard;

    // TODO: We need to unset wl_seat capabilities if this was the only keyboard
}

void qz_server_new_keyboard(qz_server* server, wlr_input_device* device)
{
    wlr_keyboard* wlr_keyboard = wlr_keyboard_from_input_device(device);

    qz_keyboard* keyboard = new qz_keyboard{};
    keyboard->server = server;
    keyboard->wlr_keyboard = wlr_keyboard;

    // We need to prepare an XKB keymap and assign it to the keyboard.
    // TODO: Configuration support for keyboard layout

    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_keymap* keymap = xkb_keymap_new_from_names(context, qz_ptr(xkb_rule_names{
        .layout = "gb",
    }), XKB_KEYMAP_COMPILE_NO_FLAGS);

    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

    keyboard->listeners.listen(&wlr_keyboard->events.modifiers, keyboard, qz_keyboard_handle_modifiers);
    keyboard->listeners.listen(&wlr_keyboard->events.key,       keyboard, qz_keyboard_handle_key);
    keyboard->listeners.listen(&      device->events.destroy,   keyboard, qz_keyboard_handle_destroy);

    wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

    server->keyboards.emplace_back(keyboard);
}

void qz_server_new_pointer(qz_server* server, wlr_input_device* device)
{
    libinput_device* libinput_device;
    if (wlr_input_device_is_libinput(device) && (libinput_device = wlr_libinput_get_device_handle(device))) {
        if (libinput_device_config_accel_is_available(libinput_device)) {
            libinput_device_config_accel_set_profile(libinput_device, LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
            libinput_device_config_accel_set_speed(  libinput_device, qz_libinput_mouse_speed);
        }
    }

    wlr_cursor_attach_input_device(server->cursor, device);
}

void qz_server_new_input(wl_listener* listener, void* data)
{
    qz_server* server = qz_listener_userdata<qz_server*>(listener);
    wlr_input_device* device = static_cast<wlr_input_device*>(data);
    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            qz_server_new_keyboard(server, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            qz_server_new_pointer(server, device);
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

void qz_seat_request_set_cursor(wl_listener* listener, void* data)
{
    qz_server* server = qz_listener_userdata<qz_server*>(listener);
    wlr_seat_pointer_request_set_cursor_event* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
    wlr_seat_client* focused_client = server->seat->pointer_state.focused_client;

    if (focused_client == event->seat_client) {
        wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

void qz_seat_pointer_focus_change(wl_listener* listener, void* data)
{
    qz_server* server = qz_listener_userdata<qz_server*>(listener);

    wlr_seat_pointer_focus_change_event* event = static_cast<wlr_seat_pointer_focus_change_event*>(data);
    if (event->new_surface == NULL) {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "default");
    }
}

void qz_seat_request_set_selection(wl_listener* listener, void* data)
{
    // TODO: Validate serial?

    qz_server* server = qz_listener_userdata<qz_server*>(listener);

    wlr_seat_request_set_selection_event* event = static_cast<wlr_seat_request_set_selection_event*>(data);
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

// -----------------------------------------------------------------------------

void qz_seat_request_start_drag(wl_listener* listener, void* data)
{
    qz_server* server = qz_listener_userdata<qz_server*>(listener);
    wlr_seat_request_start_drag_event* event = static_cast<wlr_seat_request_start_drag_event*>(data);

    if (wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial)) {
        wlr_log(WLR_ERROR, "Drag requested, start");
        wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
    } else {
        wlr_log(WLR_ERROR, "Drag requested, invalid serial, destroying..");
        wlr_data_source_destroy(event->drag->source);
    }
}

static
void qz_seat_drag_icon_destroy(wl_listener* listener, void*)
{
    wlr_log(WLR_ERROR, "Drag icon destroy");

    qz_server* server = qz_listener_userdata<qz_server*>(listener);

    // Refocus last focused toplevel
    qz_focus_toplevel(server->focused_toplevel);
    qz_process_cursor_motion(server, 0);

    qz_unlisten(qz_listener_from(listener));
}

void qz_seat_start_drag(wl_listener* listener, void* data)
{
    wlr_log(WLR_ERROR, "Drag start");

    qz_server* server = qz_listener_userdata<qz_server*>(listener);
    wlr_drag* drag = static_cast<wlr_drag*>(data);
    if (!drag->icon) return;

    drag->icon->data = &wlr_scene_drag_icon_create(server->drag_icon_parent, drag->icon)->node;
    qz_listen(&drag->icon->events.destroy, server, qz_seat_drag_icon_destroy);
}

static
void qz_seat_drag_update_position(qz_server* server)
{
    wlr_scene_node_set_position(&server->drag_icon_parent->node, int(std::round(server->cursor->x)), int(std::round(server->cursor->y)));

    // TODO: This should be on a separate layer that is always on top (above even the OVERLAY layer)
    wlr_scene_node_raise_to_top(&server->drag_icon_parent->node);
}

// -----------------------------------------------------------------------------

void qz_reset_cursor_mode(qz_server* server)
{
    server->cursor_mode = qz_cursor_mode::passthrough;
    server->grabbed_toplevel = NULL;
}

void qz_process_cursor_move(qz_server* server)
{
    qz_toplevel* toplevel = server->grabbed_toplevel;
    wlr_scene_node_set_position(&toplevel->scene_tree->node, server->cursor->x - server->grab_x, server->cursor->y - server->grab_y);
}

void qz_process_cursor_resize(qz_server* server)
{
    qz_toplevel* toplevel = server->grabbed_toplevel;
    double border_x = server->cursor->x - server->grab_x;
    double border_y = server->cursor->y - server->grab_y;
    int new_left = server->grab_geobox.x;
    int new_right = server->grab_geobox.x + server->grab_geobox.width;
    int new_top = server->grab_geobox.y;
    int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

    if (server->resize_edges & WLR_EDGE_TOP) {
        new_top = border_y;
        if (new_top >= new_bottom) {
            new_top = new_bottom - 1;
        }
    } else if (server->resize_edges & WLR_EDGE_BOTTOM) {
        new_bottom = border_y;
        if (new_bottom <= new_top) {
            new_bottom = new_top + 1;
        }
    }

    if (server->resize_edges & WLR_EDGE_LEFT) {
        new_left = border_x;
        if (new_left >= new_right) {
            new_left = new_right - 1;
        }
    } else if (server->resize_edges & WLR_EDGE_RIGHT) {
        new_right = border_x;
        if (new_right <= new_left) {
            new_right = new_left + 1;
        }
    }

    wlr_box* geo_box = &toplevel->xdg_toplevel->base->geometry;
    wlr_box bounds {
        .x = new_left - geo_box->x,
        .y = new_top - geo_box->y,
        .width = new_right - new_left,
        .height = new_bottom - new_top,
    };
    // TODO: Investigate issue with GLFW/SDL Vulkan windows slow resizing
    qz_toplevel_set_bounds(toplevel, bounds);
}

void qz_process_cursor_motion(qz_server* server, uint32_t time_msecs)
{
    qz_seat_drag_update_position(server);

    if (server->cursor_mode == qz_cursor_mode::move) {
        qz_process_cursor_move(server);
        return;
    } else if (server->cursor_mode == qz_cursor_mode::resize) {
        qz_process_cursor_resize(server);
        return;
    } else if (server->cursor_mode == qz_cursor_mode::zone) {
        qz_zone_process_cursor_motion(server);
        return;
    }

    double sx, sy;
    wlr_seat* seat = server->seat;
    wlr_surface* surface = nullptr;
    qz_toplevel* toplevel = qz_get_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (!toplevel) {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "default");
    }

    if (surface) {
        // TODO: If mouse button held down, send mouse motion events to window that button was pressed in
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time_msecs, sx, sy);
    } else {
        wlr_seat_pointer_notify_clear_focus(seat);
    }
}

void qz_server_cursor_motion(wl_listener* listener, void* data)
{
    qz_server* server = qz_listener_userdata<qz_server*>(listener);
    wlr_pointer_motion_event* event = static_cast<wlr_pointer_motion_event*>(data);

    // TODO: Handle custom acceleration here

    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    qz_process_cursor_motion(server, event->time_msec);
}

void qz_server_cursor_motion_absolute(wl_listener* listener, void* data)
{
    // TODO: Drawing tablet handling?

    qz_server* server = qz_listener_userdata<qz_server*>(listener);
    wlr_pointer_motion_absolute_event* event = static_cast<wlr_pointer_motion_absolute_event*>(data);

    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    qz_process_cursor_motion(server, event->time_msec);
}

void qz_server_cursor_button(wl_listener* listener, void* data)
{
    qz_server* server = qz_listener_userdata<qz_server*>(listener);
    wlr_pointer_button_event* event = static_cast<wlr_pointer_button_event*>(data);

    // TODO: Focus follows mouse?
    // TODO: Move interaction logic to separate source

    // Zone interaction

    if (qz_zone_process_cursor_button(server, event)) return;

    // End move/size interaction

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        qz_reset_cursor_mode(server);
        wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
        return;
    }

    // Focus window on any button press

    double sx, sy;
    wlr_surface* surface = nullptr;
    qz_toplevel* toplevel = qz_get_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (toplevel) {
        qz_focus_toplevel(toplevel);
    }

    // Check for move/size interaction begin

    if (toplevel && qz_is_main_mod_down(server)) {
        if (event->button == BTN_LEFT && (qz_get_modifiers(server) & WLR_MODIFIER_SHIFT)) {
            qz_toplevel_begin_interactive(toplevel, qz_cursor_mode::move, 0);
            return;
        } else if (event->button == BTN_RIGHT) {
            wlr_box bounds = qz_client_get_bounds(toplevel);
            int nine_slice_x = ((server->cursor->x - bounds.x) * 3) / bounds.width;
            int nine_slice_y = ((server->cursor->y - bounds.y) * 3) / bounds.height;

            qz_cursor_mode type = qz_cursor_mode::resize;
            uint32_t edges = 0;

            if      (nine_slice_x < 1) edges |= WLR_EDGE_LEFT;
            else if (nine_slice_x > 1) edges |= WLR_EDGE_RIGHT;

            if      (nine_slice_y < 1) edges |= WLR_EDGE_TOP;
            else if (nine_slice_y > 1) edges |= WLR_EDGE_BOTTOM;

            // If no edges selected, must be center - switch to move
            if (!edges) type = qz_cursor_mode::move;

            qz_toplevel_begin_interactive(toplevel, type, edges);
            return;
        }
    }

    // ... else passthrough to client

    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

void qz_server_cursor_axis(wl_listener* listener, void* data)
{
    qz_server* server = qz_listener_userdata<qz_server*>(listener);
    wlr_pointer_axis_event* event = static_cast<wlr_pointer_axis_event*>(data);

    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta, event->delta_discrete, event->source, event->relative_direction);
}

void qz_server_cursor_frame(wl_listener* listener, void*)
{
    qz_server* server = qz_listener_userdata<qz_server*>(listener);

    wlr_seat_pointer_notify_frame(server->seat);
}
