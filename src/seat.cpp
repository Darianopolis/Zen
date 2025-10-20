#include "pch.hpp"
#include "core.hpp"

static
uint32_t get_modifiers(Server* server)
{
    wlr_keyboard* keyboard = wlr_seat_get_keyboard(server->seat);
    if (!keyboard) return 0;
    return wlr_keyboard_get_modifiers(keyboard);
}

bool is_mod_down(Server* server, wlr_keyboard_modifier modifiers)
{
    return (get_modifiers(server) & modifiers) == modifiers;
}

bool is_main_mod_down(Server* server)
{
    return get_modifiers(server) & server->main_modifier;
}

bool is_cursor_visible(Server* server)
{
    return server->pointer.cursor_is_visible;
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
        if ((handled = input_handle_key(server, *event, sym))) {
            break;
        }
    }

    if (handled) return;

    wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
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

static
std::string pointer_to_string(Pointer* pointer)
{
    return pointer ? std::format("Pointer<{}>", (void*)pointer) : "nullptr";
}

void pointer_destroy(wl_listener* listener, void*)
{
    Pointer* pointer = listener_userdata<Pointer*>(listener);

    log_info("Pointer destroyed: {}", pointer_to_string(pointer));

    std::erase(pointer->server->pointers, pointer);

    delete pointer;
}

uint32_t get_num_pointer_buttons_down(Server* server)
{
    uint32_t count = 0;
    for (Pointer* pointer : server->pointers) {
        count += pointer->wlr_pointer->button_count;
    }
    return count;
}

void server_new_pointer(Server* server, wlr_input_device* device)
{
    Pointer* pointer = new Pointer{};
    pointer->server = server;
    pointer->wlr_pointer = wlr_pointer_from_input_device(device);
    pointer->wlr_pointer->data = pointer;

    log_info("Pointer created:   {}", pointer_to_string(pointer));

    server->pointers.emplace_back(pointer);

    libinput_device* libinput_device;
    if (wlr_input_device_is_libinput(device) && (libinput_device = wlr_libinput_get_device_handle(device))) {
        if (libinput_device_config_accel_is_available(libinput_device)) {
            libinput_device_config_accel_set_profile(libinput_device, LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
            libinput_device_config_accel_set_speed(  libinput_device, libinput_mouse_speed);
        }
    }

    pointer->listeners.listen(&device->events.destroy, pointer, pointer_destroy);

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

// -----------------------------------------------------------------------------

static
void update_cursor_debug_visual_position(Server* server)
{
    if (!server->pointer.debug_visual) return;

    int32_t he = server->pointer.debug_visual_half_extent;
    wlr_scene_node_set_position(&server->pointer.debug_visual->node,
        server->cursor->x + (server->debug.is_nested ? 0 : -he*2),
        server->cursor->y - he*2);
}

void update_cursor_state(Server* server)
{
    Color debug_visual_color;

    server->pointer.cursor_is_visible = true;
    if (Surface* focused_surface = Surface::from(server->seat->pointer_state.focused_surface); focused_surface && focused_surface->cursor.surface_set) {
        wlr_surface* cursor_surface = focused_surface->cursor.surface;
        bool visible = cursor_surface && cursor_surface->current.width && cursor_surface->current.height;
        if (visible || server->seat->pointer_state.focused_client == server->seat->keyboard_state.focused_client) {
            log_debug("Cursor state: Restoring cursor surface (surface = {}, visible = {}", (void*)cursor_surface, visible);
            server->pointer.cursor_is_visible = visible;
            wlr_cursor_set_surface(server->cursor, cursor_surface, focused_surface->cursor.hotspot_x, focused_surface->cursor.hotspot_y);
            debug_visual_color = visible ? Color{0, 1, 0, 0.5} : Color{1, 0, 0, 0.5};
        } else {
            log_debug("Cursor state: Client not allowed to hide cursor, using default");
            wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "default");
            debug_visual_color = Color{1, 1, 0, 0.5};
        }
    } else {
        log_debug("Cursor state: No surface focus or surface cursor unset, using default");
        wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "default");
                debug_visual_color = Color{1, 0, 1, 0.5};
    }

    // Update debug visual

    if (server->pointer.debug_visual) {
        wlr_scene_rect_set_color(server->pointer.debug_visual, debug_visual_color.values);
        update_cursor_debug_visual_position(server);
    }
}

void seat_request_set_cursor(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_seat_pointer_request_set_cursor_event* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);

    if (server->seat->pointer_state.focused_client != event->seat_client) {
        log_error("Cursor request from unfocused client {}", client_to_string(event->seat_client->client));
        return;
    }

    Surface* requestee_surface = Surface::from(server->seat->pointer_state.focused_surface);

    if (!requestee_surface) {
        log_warn("Cursor request (surface = {:14}) for {}", (void*)event->surface, client_to_string(event->seat_client->client));
        log_warn("  focused client does not have currently focused surface, ignoring...");
        return;
    }

    log_info("Cursor request (surface = {:14}) for {}", (void*)event->surface, surface_to_string(requestee_surface));
    requestee_surface->cursor.surface = event->surface;
    requestee_surface->cursor.hotspot_x = event->hotspot_x;
    requestee_surface->cursor.hotspot_y = event->hotspot_y;
    requestee_surface->cursor.surface_set = true;

    update_cursor_state(server);

    if (event->surface && !event->surface->data) {

        // Since a client may request a cursor with a currently visible surface and then commit an invisible state
        // we register a listener to track this and reset the cursor as necessary, while also updating the debug cursor visual state

        struct CursorListener : Surface
        {
            // This listener inherits from Surface with an `invalid` role so that
            // Surface::from(struct wlr_surface*) calls are still always safe to make

            Server* server;
            wlr_seat_client* client;
            ListenerSet listeners;

            // TODO: Store Surface* and have Surface store CursorListener* to ensure lifetime safety
            struct wlr_surface* requestee_surface;

            static void commit(wl_listener* listener, void*)
            {
                CursorListener* cursor_listener = listener_userdata<CursorListener*>(listener);
                update_cursor_state(cursor_listener->server);
            }

            static void destroy(wl_listener* listener, void* data)
            {
                CursorListener* cursor_listener = listener_userdata<CursorListener*>(listener);
                struct wlr_surface* surface = static_cast<struct wlr_surface*>(data);
                Surface* requestee_surface = Surface::from(cursor_listener->requestee_surface);
                if (requestee_surface && surface == requestee_surface->cursor.surface) {
                    requestee_surface->cursor.surface = nullptr;
                    requestee_surface->cursor.surface_set = false;
                    update_cursor_state(cursor_listener->server);
                }
                delete cursor_listener;
            }
        };

        CursorListener* cursor_listener = new CursorListener {};
        cursor_listener->server = server;
        cursor_listener->client = event->seat_client;
        cursor_listener->requestee_surface = requestee_surface->wlr_surface;
        cursor_listener->listeners.listen(&event->surface->events.commit, cursor_listener, CursorListener::commit);
        cursor_listener->listeners.listen(&event->surface->events.destroy, cursor_listener, CursorListener::destroy);

        event->surface->data = cursor_listener;
    }
}

void seat_pointer_focus_change(wl_listener* listener, void*)
{
    Server* server = listener_userdata<Server*>(listener);

    update_cursor_state(server);
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

    process_cursor_motion(server, 0, nullptr, 0, 0, 0, 0, 0, 0);

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
}

// -----------------------------------------------------------------------------

void server_pointer_constraint_destroy(wl_listener* listener, void* data)
{
    wlr_pointer_constraint_v1* constraint = static_cast<wlr_pointer_constraint_v1*>(data);

    log_info("Pointer constraint destroyed: {}", pointer_constraint_to_string(constraint));

    Server* server = listener_userdata<Server*>(listener);
    if (server->pointer.active_constraint == constraint) {
        server->pointer.active_constraint = nullptr;
    }

    unlisten(listener_from(listener));
}

void server_pointer_constraint_new(wl_listener* listener, void* data)
{
    wlr_pointer_constraint_v1* constraint = static_cast<wlr_pointer_constraint_v1*>(data);
    log_info("Pointer constraint created: {} for {}", pointer_constraint_to_string(constraint), toplevel_to_string(Toplevel::from(constraint->surface)));

    Server* server = listener_userdata<Server*>(listener);

    listen(&constraint->events.destroy, server, server_pointer_constraint_destroy);
}

// -----------------------------------------------------------------------------

void set_interaction_mode(Server* server, InteractionMode mode)
{
    // log_warn("Interaction mode transition {} -> {}", magic_enum::enum_name(server->interaction_mode), magic_enum::enum_name(mode));

    // TODO: Interlocks

    InteractionMode prev_mode = server->interaction_mode;

    if (prev_mode == InteractionMode::focus_cycle) {
        focus_cycle_end(server);
    }

    if (prev_mode == InteractionMode::zone) {
        zone_end_selection(server);
    }

    server->interaction_mode = mode;

    if (prev_mode == InteractionMode::move || prev_mode == InteractionMode::resize) {
        server->movesize.grabbed_toplevel = nullptr;
    }
}

void process_cursor_move(Server* server)
{
    Toplevel* toplevel = server->movesize.grabbed_toplevel;

    wlr_box bounds = surface_get_bounds(toplevel);
    bounds.x = server->movesize.grab_bounds.x + int(server->cursor->x - server->movesize.grab.x);
    bounds.y = server->movesize.grab_bounds.y + int(server->cursor->y - server->movesize.grab.y);
    toplevel_set_bounds(toplevel, bounds);
}

void process_cursor_resize(Server* server)
{
    auto& movesize = server->movesize;

    int dx = int(server->cursor->x - movesize.grab.x);
    int dy = int(server->cursor->y - movesize.grab.y);

    int left   = movesize.grab_bounds.x;
    int top    = movesize.grab_bounds.y;
    int right  = movesize.grab_bounds.x + movesize.grab_bounds.width;
    int bottom = movesize.grab_bounds.y + movesize.grab_bounds.height;

    if      (movesize.resize_edges & WLR_EDGE_TOP)    top    = std::min(top    + dy, bottom - 1);
    else if (movesize.resize_edges & WLR_EDGE_BOTTOM) bottom = std::max(bottom + dy, top    + 1);

    if      (movesize.resize_edges & WLR_EDGE_LEFT)  left  = std::min(left  + dx, right - 1);
    else if (movesize.resize_edges & WLR_EDGE_RIGHT) right = std::max(right + dx, left  + 1);

    toplevel_set_bounds(movesize.grabbed_toplevel, {
        .x = left,
        .y = top,
        .width  = right  - left,
        .height = bottom - top,
    });
}

void process_cursor_motion(Server* server, uint32_t time_msecs, wlr_input_device* device, double dx, double dy, double rel_dx, double rel_dy, double dx_unaccel, double dy_unaccel)
{
    Defer _ = [&] {
        update_cursor_debug_visual_position(server);
    };

    // Handle compositor interactions

    if (time_msecs) {
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
    }

    // Get focused surface

    double sx = 0, sy = 0;
    wlr_seat* seat = server->seat;
    Surface* surface = nullptr;
    struct wlr_surface* wlr_surface = nullptr;
    if (get_num_pointer_buttons_down(server) > 0 && (surface = Surface::from(seat->pointer_state.focused_surface))) {
        wlr_surface = surface->wlr_surface;
        wlr_box coord_system = surface_get_coord_system(surface);
        sx = server->cursor->x - coord_system.x;
        sy = server->cursor->y - coord_system.y;
    }

    if (!wlr_surface) {
        surface = get_surface_at(server, server->cursor->x, server->cursor->y, &wlr_surface, &sx, &sy);
    }

    // Handle constraints and update mouse

    {
        // log_info("Pointer motion ({:7.2f}, {:7.2f}) rel ({:7.2f}, {:7.2f})", dx, dy, rel_dx, rel_dy);

        // log_info("Pointer motion\n{}  surface = {}\n{}  pointer surface = {}\n{}  keyboard surface = {}",
        //     log_indent, surface_to_string(surface),
        //     log_indent, surface_to_string(Surface::from(server->seat->pointer_state.focused_surface)),
        //     log_indent, surface_to_string(Surface::from(server->seat->keyboard_state.focused_surface)));

        if (rel_dx || rel_dy || dx_unaccel || dy_unaccel) {
            // if (server->seat->pointer_state.focused_surface && server->seat->pointer_state.focused_surface == server->seat->keyboard_state.focused_surface) {
            if (surface == get_focused_surface(server)) {
                // Only send relative pointer motion when pointer focus is keyboard focus
                // (some applications will try to handle relative pointer input even when they're not focused)
                wlr_relative_pointer_manager_v1_send_relative_motion(server->pointer.relative_pointer_manager, server->seat, uint64_t(time_msecs) * 1000, rel_dx, rel_dy, dx_unaccel, dy_unaccel);
            }
        }

        bool constraint_active = false;
        Defer _ = [&] {
            if (!constraint_active && server->pointer.active_constraint) {
                log_info("Pointer constraint deactivated: {} (reason: no constraints active)", pointer_constraint_to_string(server->pointer.active_constraint));
                wlr_pointer_constraint_v1_send_deactivated(server->pointer.active_constraint);
                server->pointer.active_constraint = nullptr;
            }
        };

        if (surface && surface == get_focused_surface(server)) {
            wlr_pointer_constraint_v1* constraint =
                wlr_pointer_constraints_v1_constraint_for_surface(server->pointer.pointer_constraints, wlr_surface, server->seat);
            if (constraint) {
                if (constraint != server->pointer.active_constraint) {
                    if (server->pointer.active_constraint) {
                        log_info("Pointer constraint deactivated: {} (reason: replacing with new constraint)", pointer_constraint_to_string(server->pointer.active_constraint));
                        wlr_pointer_constraint_v1_send_deactivated(server->pointer.active_constraint);
                    }
                    log_info("Pointer constraint activated: {}", pointer_constraint_to_string(constraint));
                    wlr_pointer_constraint_v1_send_activated(constraint);
                    server->pointer.active_constraint = constraint;
                }

                constraint_active = true;

                double sx_confined, sy_confined;
                bool contained = wlr_region_confine(&constraint->region, sx, sy, sx + dx, sy + dy, &sx_confined, &sy_confined);

                // wlr_box bounds = surface_get_bounds(surface);
                // log_debug("Confine bounds ({:4}, {:4}) ({:4}, {:4}) old ({:4.0f}, {:4.0f}) new ({:4.0f}, {:4.0f}) confined ({:4.0f}, {:4.0f}) contained = {}",
                //     bounds.x, bounds.y, bounds.width, bounds.height,
                //     sx, sy,
                //     sx + dx, sy + dy,
                //     sx_confined, sy_confined,
                //     contained);

                if (contained) {
                    if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
                        wlr_seat_pointer_notify_enter(seat, wlr_surface, sx, sy);
                        return;
                    }

                    dx = sx_confined - sx;
                    dy = sy_confined - sy;
                }
            }
        }

        wlr_cursor_move(server->cursor, device, dx, dy);
    }

    // Notify

    if (wlr_surface) {
        if (!time_msecs) {
            timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            time_msecs = now.tv_sec * 1000 + now.tv_nsec / 1000'000;
        }

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

    process_cursor_motion(server, event->time_msec, &event->pointer->base, event->delta_x, event->delta_y, event->delta_x, event->delta_y, event->unaccel_dx, event->unaccel_dy);
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

    Pointer* pointer = Pointer::from(event->pointer);

    double dx = lx - server->cursor->x;
    double dy = ly - server->cursor->y;
    double rel_dx = (lx - pointer->last_abs_x) * pointer_abs_to_rel_speed_multiplier;
    double rel_dy = (ly - pointer->last_abs_y) * pointer_abs_to_rel_speed_multiplier;
    pointer->last_abs_x = lx;
    pointer->last_abs_y = ly;
    process_cursor_motion(server, event->time_msec, &event->pointer->base, dx, dy, rel_dx, rel_dy, rel_dx, rel_dy);
}

void server_cursor_button(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_pointer_button_event* event = static_cast<wlr_pointer_button_event*>(data);

    if (input_handle_button(server, *event)) {
        return;
    }

    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

void server_cursor_axis(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_pointer_axis_event* event = static_cast<wlr_pointer_axis_event*>(data);

    if (input_handle_axis(server, *event)) return;

    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta, event->delta_discrete, event->source, event->relative_direction);
}

void server_cursor_frame(wl_listener* listener, void*)
{
    Server* server = listener_userdata<Server*>(listener);

    wlr_seat_pointer_notify_frame(server->seat);
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

bool input_handle_key(Server* server, const wlr_keyboard_key_event& event, xkb_keysym_t sym)
{
    wl_keyboard_key_state state = event.state;

    // log_trace("Key {:#6x} -> {}", sym, magic_enum::enum_name(state));

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED && is_main_mod_down(server)) {
        switch (sym)
        {
            case XKB_KEY_Escape:
                wl_display_terminate(server->display);
                break;
            case XKB_KEY_Tab:
            case XKB_KEY_ISO_Left_Tab: {
                bool do_cycle = true;
                if (server->interaction_mode ==  InteractionMode::passthrough) {
                    do_cycle = Toplevel::from(get_focused_surface(server));
                    focus_cycle_begin(server, nullptr);
                }
                if (do_cycle && server->interaction_mode == InteractionMode::focus_cycle) {
                    focus_cycle_step(server, nullptr, sym == XKB_KEY_ISO_Left_Tab);
                }
                return true;
            }
            case XKB_KEY_t:
                spawn("konsole", {"konsole"});
                return true;
            case XKB_KEY_T:
                spawn("konsole", {"konsole", "--workdir", server->debug.original_cwd.c_str()});
                return true;
            case XKB_KEY_g:
                spawn("dolphin", {"dolphin"});
                return true;
            case XKB_KEY_h:
                spawn("kalk", {"kalk"});
                return true;
            case XKB_KEY_d:
                spawn("rofi", {"rofi", "-show", "drun"});
                return true;
            case XKB_KEY_D:
                spawn("rofi", {"rofi", "-show", "run"});
                return true;
            case XKB_KEY_v:
                spawn("pavucontrol", {"pavucontrol"});
                return true;
            case XKB_KEY_n:
                spawn("systemctl", {"systemctl", "suspend"});
                return true;
            case XKB_KEY_i:
                spawn("xeyes", {"xeyes"});
                return true;
            case XKB_KEY_s:
                surface_unfocus(get_focused_surface(server), false);
                return true;
            case XKB_KEY_q:
                if (Toplevel* focused = Toplevel::from(get_focused_surface(server))) {
                    wlr_xdg_toplevel_send_close(focused->xdg_toplevel());
                }
                return true;
            case XKB_KEY_f:
                if (Toplevel* focused = Toplevel::from(get_focused_surface(server))) {
                    toplevel_set_fullscreen(focused, !focused->xdg_toplevel()->current.fullscreen);
                }
                return true;
            default:
                ;
        }
    }

    if (state == WL_KEYBOARD_KEY_STATE_RELEASED && server->interaction_mode == InteractionMode::focus_cycle) {
        if (sym == server->main_modifier_keysym_left || sym == server->main_modifier_keysym_right) {
            focus_cycle_end(server);
            return true;
        }
    }

    return false;
}

bool input_handle_axis(Server* server, const wlr_pointer_axis_event& event)
{
    // log_trace("Axis {} -> {}", magic_enum::enum_name(event.orientation), event.delta);

    if (is_main_mod_down(server) && event.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        if (server->interaction_mode ==  InteractionMode::passthrough) {
            focus_cycle_begin(server, server->cursor);
        }
        if (server->interaction_mode == InteractionMode::focus_cycle) {
            focus_cycle_step(server, server->cursor, event.delta > 0);
        }

        return true;
    }

    return false;
}

bool input_handle_button(Server* server, const wlr_pointer_button_event& event)
{
    // log_trace("Button {} -> {}", libevdev_event_code_get_name(EV_KEY, event.button), magic_enum::enum_name(event.state));

    // Handle interrupt focus cycle

    bool focus_cycle_interrupted = event.state == WL_POINTER_BUTTON_STATE_PRESSED && server->interaction_mode == InteractionMode::focus_cycle;
    if (focus_cycle_interrupted) {
        focus_cycle_end(server);
        focus_cycle_interrupted = true;
    }

    double sx, sy;
    wlr_surface* surface = nullptr;
    Surface* surface_under_cursor = get_surface_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    if (focus_cycle_interrupted && surface_under_cursor != get_focused_surface(server)) {
        // If we interrupted a focus cycle by clicking a previously hidden toplevel,
        // don't transfer focus as it wouldn't have been visible to the user before the press
        if (Toplevel::from(surface_under_cursor)) {
            surface_unfocus(get_focused_surface(server), false);
        } else {
            surface_focus(surface_under_cursor);
        }
        return true;
    }

    // Zone interaction

    if (server->interaction_mode == InteractionMode::passthrough || server->interaction_mode == InteractionMode::zone) {
        if (zone_process_cursor_button(server, event) || server->interaction_mode == InteractionMode::zone) {
            return true;
        }
    }

    // Leave move/size modes on all mouse buttons released

    if (server->interaction_mode == InteractionMode::move || server->interaction_mode == InteractionMode::resize) {
        if (event.state == WL_POINTER_BUTTON_STATE_RELEASED && !get_num_pointer_buttons_down(server)) {
            set_interaction_mode(server, InteractionMode::passthrough);
        }
        return true;
    }

    // No further actions for button releases

    if (event.state == WL_POINTER_BUTTON_STATE_RELEASED) {
        return false;
    }

    // Check for move/size interaction begin, or close-under-cursor

    if (Toplevel* toplevel = Toplevel::from(surface_under_cursor); toplevel && is_main_mod_down(server) && server->interaction_mode == InteractionMode::passthrough) {
        if (is_cursor_visible(server)) {
            if (event.button == BTN_LEFT && is_mod_down(server, WLR_MODIFIER_SHIFT)) {
                toplevel_begin_interactive(toplevel, InteractionMode::move);
            } else if (event.button == BTN_RIGHT) {
                toplevel_begin_interactive(toplevel, InteractionMode::resize);
            } else if (event.button == BTN_MIDDLE) {
                wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel());
            }
        } else {
            log_warn("Compositor button pressed while cursor is hidden");
        }
        return true;
    }

    // Focus window on any button press (only switch focus if no previous focus)

    if (get_num_pointer_buttons_down(server) == 1 || !get_focused_surface(server)) {
        if (surface_under_cursor) {
            Surface* prev_focus = get_focused_surface(server);
            surface_focus(surface_under_cursor);
            if (prev_focus != get_focused_surface(server) && !is_cursor_visible(server)) {
                log_warn("Button press event suppressed (reason: pointer hidden after moving focus to new window)");
                return true;
            }
        } else if (get_focused_surface(server)) {
            surface_unfocus(get_focused_surface(server), false);
        }
    }

    return false;
}
