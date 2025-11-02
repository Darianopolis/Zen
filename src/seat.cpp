#include "pch.hpp"
#include "core.hpp"

#define NOISY_POINTERS 0

static
void update_seat_caps(Server* server)
{
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!server->keyboards.empty()) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }

    wlr_seat_set_capabilities(server->seat, caps);
}

Modifiers get_modifiers(Server* server)
{
    wlr_keyboard* keyboard = wlr_seat_get_keyboard(server->seat);
    uint32_t key_mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;

    Modifiers mods = {};
    if (key_mods & WLR_MODIFIER_LOGO)     mods |= Modifiers::Super;
    if (key_mods & WLR_MODIFIER_SHIFT)    mods |= Modifiers::Shift;
    if (key_mods & WLR_MODIFIER_CTRL)     mods |= Modifiers::Ctrl;
    if (key_mods & WLR_MODIFIER_ALT)      mods |= Modifiers::Alt;
    if (key_mods & server->main_modifier) mods |= Modifiers::Mod;

    for (Pointer* pointer : server->pointers) {
        for (uint32_t i = 0; i < pointer->wlr_pointer->button_count; ++i) {
            if (pointer->wlr_pointer->buttons[i] == pointer_modifier_button) {
                mods |= Modifiers::Mod;
                break;
            }
        }
    }

    return mods;
}

bool check_mods(Server* server, Modifiers required_mods)
{
    return get_modifiers(server) >= required_mods;
}

// -----------------------------------------------------------------------------

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

    update_seat_caps(keyboard->server);

    delete keyboard;

    // TODO: We need to unset wl_seat capabilities if this was the only keyboard
}

void seat_keyboard_focus_change(wl_listener*, void* data)
{
    wlr_seat_keyboard_focus_change_event* event = static_cast<wlr_seat_keyboard_focus_change_event*>(data);

    if (Toplevel* toplevel = Toplevel::from(event->old_surface)) toplevel_update_border(toplevel);
    if (Toplevel* toplevel = Toplevel::from(event->new_surface)) toplevel_update_border(toplevel);
}

void keyboard_new(Server* server, wlr_input_device* device)
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

    if (wlr_input_device_is_libinput(device)) {

        // Set default numlock state

        xkb_mod_index_t numlock_idx = xkb_keymap_mod_get_index(wlr_keyboard->keymap, XKB_MOD_NAME_NUM);
        wlr_keyboard_modifiers mods = wlr_keyboard->modifiers;
        mods.locked = (mods.locked & ~(1 << numlock_idx)) | (keyboard_default_numlock_state << numlock_idx);
        wlr_keyboard_notify_modifiers(wlr_keyboard, mods.depressed, mods.latched, mods.locked, mods.group);
    }
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
        for (uint32_t i = 0; i < pointer->wlr_pointer->button_count; ++i) {
            if (pointer->wlr_pointer->buttons[i] != pointer_modifier_button) {
                count++;
            }
        }
    }
    return count;
}

void pointer_new(Server* server, wlr_input_device* device)
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
            libinput_device_config_accel_set_speed(  libinput_device, 0);
        }
    }

    pointer->listeners.listen(&device->events.destroy, pointer, pointer_destroy);

    wlr_cursor_attach_input_device(server->cursor, device);
}

void input_new(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_input_device* device = static_cast<wlr_input_device*>(data);
    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            keyboard_new(server, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            pointer_new(server, device);
            break;
        default:
            break;
    }

    update_seat_caps(server);
}

// -----------------------------------------------------------------------------

vec2 get_cursor_pos(Server* server)
{
    return { server->cursor->x, server->cursor->y };
}

bool is_cursor_visible(Server* server)
{
    return server->pointer.cursor_is_visible;
}

bool cursor_surface_is_visible(CursorSurface* cursor_surface)
{
    return cursor_surface->wlr_surface->current.width && cursor_surface->wlr_surface->current.height;
}

void cursor_surface_commit(wl_listener* listener, void*)
{
    CursorSurface* cursor_listener = listener_userdata<CursorSurface*>(listener);
    update_cursor_state(cursor_listener->server);
}

void cursor_surface_destroy(wl_listener* listener, void*)
{
    CursorSurface* cursor_surface = listener_userdata<CursorSurface*>(listener);

#if NOISY_POINTERS
    log_info("Cursor destroyed: {}", cursor_surface_to_string(cursor_surface));
#endif

    Server* server = cursor_surface->server;

    delete cursor_surface;

    update_cursor_state(server);
}

static
void update_cursor_debug_visual_position(Server* server)
{
    if (!server->pointer.debug_visual_enabled) return;

    int32_t he = server->pointer.debug_visual_half_extent;
    wlr_scene_node_set_position(&server->pointer.debug_visual->node,
        get_cursor_pos(server).x + (server->session.is_nested ? 0 : -he*2),
        get_cursor_pos(server).y - he*2);
}

void update_cursor_state(Server* server)
{
    fvec4 debug_visual_color;

    server->pointer.cursor_is_visible = true;
    if (Surface* focused_surface = Surface::from(server->seat->pointer_state.focused_surface); focused_surface && focused_surface->cursor.surface_set) {
        CursorSurface* cursor_surface = focused_surface->cursor.surface.get();
        bool visible = cursor_surface && cursor_surface_is_visible(cursor_surface);
        if (visible || server->seat->pointer_state.focused_client == server->seat->keyboard_state.focused_client) {
            // log_debug("Cursor state: Restoring cursor {}", cursor_surface_to_string(cursor_surface));
            server->pointer.cursor_is_visible = visible;

            wlr_cursor_set_surface(server->cursor, cursor_surface ? cursor_surface->wlr_surface : nullptr, focused_surface->cursor.hotspot_x, focused_surface->cursor.hotspot_y);
            debug_visual_color = visible ? premultiply({0, 1, 0, 0.5}) : premultiply({1, 0, 0, 0.5});
        } else {
            // log_debug("Cursor state: Client not allowed to hide cursor, using default");
            wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "default");
            debug_visual_color = premultiply({1, 1, 0, 0.5});
        }
    } else {
        // log_debug("Cursor state: No surface focus or surface cursor unset, using default");
        wlr_cursor_set_xcursor(server->cursor, server->cursor_manager, "default");
        debug_visual_color = premultiply({1, 0, 1, 0.5});
    }

    // Update debug visual

    wlr_scene_node_set_enabled(&server->pointer.debug_visual->node, server->pointer.debug_visual_enabled);
    if (server->pointer.debug_visual_enabled) {
        wlr_scene_rect_set_color(server->pointer.debug_visual, glm::value_ptr(debug_visual_color));
        update_cursor_debug_visual_position(server);
    }
}

void seat_request_set_cursor(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_seat_pointer_request_set_cursor_event* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);

    Surface* requestee_surface = Surface::from(server->seat->pointer_state.focused_surface);

    if (server->seat->pointer_state.focused_client != event->seat_client || !requestee_surface) {
#if NOISY_POINTERS
        log_warn("Cursor request from unfocused client {}, ignoring...", client_to_string(event->seat_client->client));
#endif
        return;
    }

    CursorSurface* cursor_surface = nullptr;
    if (event->surface) {
        if (event->surface->data) {
            cursor_surface = static_cast<CursorSurface*>(event->surface->data);
        } else {

            cursor_surface = new CursorSurface {};
            cursor_surface->server = server;

            cursor_surface->wlr_surface = event->surface;
            cursor_surface->listeners.listen(&event->surface->events.commit, cursor_surface, cursor_surface_commit);
            cursor_surface->listeners.listen(&event->surface->events.destroy, cursor_surface, cursor_surface_destroy);

            event->surface->data = cursor_surface;

#if NOISY_POINTERS
            log_info("Cursor created:   {}", cursor_surface_to_string(cursor_surface));
#endif
        }
    }

    requestee_surface->cursor.surface = weak_from(cursor_surface);
    requestee_surface->cursor.hotspot_x = event->hotspot_x;
    requestee_surface->cursor.hotspot_y = event->hotspot_y;
    requestee_surface->cursor.surface_set = true;

    update_cursor_state(server);
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

    process_cursor_motion(server, 0, nullptr, {}, {}, {});

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
    wlr_scene_node_set_position(&server->drag_icon_parent->node, get_cursor_pos(server).x, get_cursor_pos(server).y);
}

// -----------------------------------------------------------------------------

struct PointerConstraint
{
    Server* server;
    wlr_pointer_constraint_v1* constraint;

    ListenerSet listeners;
};

void pointer_constraint_destroy(wl_listener* listener, void*)
{
    PointerConstraint* constraint = listener_userdata<PointerConstraint*>(listener);

#if NOISY_POINTERS
    log_info("Pointer constraint destroyed: {}", pointer_constraint_to_string(constraint->constraint));
#endif

    if (constraint->server->pointer.active_constraint == constraint->constraint) {
        constraint->server->pointer.active_constraint = nullptr;
    }

    delete constraint;
}

void server_pointer_constraint_set_region(wl_listener* listener, void*)
{
    PointerConstraint* pointer_constriant = listener_userdata<PointerConstraint*>(listener);

    process_cursor_motion(pointer_constriant->server, 0, nullptr, {}, {}, {});
}

void pointer_constraint_new(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_pointer_constraint_v1* constraint = static_cast<wlr_pointer_constraint_v1*>(data);

#if NOISY_POINTERS
    log_info("Pointer constraint created: {} for {}", pointer_constraint_to_string(constraint), toplevel_to_string(Toplevel::from(constraint->surface)));
#endif

    PointerConstraint* pointer_constraint = new PointerConstraint{};
    pointer_constraint->server = server;
    pointer_constraint->constraint = constraint;

    pointer_constraint->listeners.listen(&constraint->events.destroy,    pointer_constraint, pointer_constraint_destroy);
    pointer_constraint->listeners.listen(&constraint->events.set_region, pointer_constraint, server_pointer_constraint_set_region);
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
        server->movesize.grabbed_toplevel.reset();
    }
}

void process_cursor_move(Server* server)
{
    Toplevel* toplevel = server->movesize.grabbed_toplevel.get();
    if (!toplevel) {
        set_interaction_mode(server, InteractionMode::passthrough);
        return;
    }

    wlr_box bounds = surface_get_bounds(toplevel);
    bounds.x = server->movesize.grab_bounds.x + int(get_cursor_pos(server).x - server->movesize.grab.x);
    bounds.y = server->movesize.grab_bounds.y + int(get_cursor_pos(server).y - server->movesize.grab.y);
    toplevel_set_bounds(toplevel, bounds);
}

void process_cursor_resize(Server* server)
{
    Toplevel* toplevel = server->movesize.grabbed_toplevel.get();
    if (!toplevel) {
        set_interaction_mode(server, InteractionMode::passthrough);
        return;
    }

    auto& movesize = server->movesize;

    ivec2 delta = vec2(get_cursor_pos(server)) - movesize.grab;

    int left   = movesize.grab_bounds.x;
    int top    = movesize.grab_bounds.y;
    int right  = movesize.grab_bounds.x + movesize.grab_bounds.width;
    int bottom = movesize.grab_bounds.y + movesize.grab_bounds.height;

    if      (movesize.resize_edges & WLR_EDGE_TOP)    top    = std::min(top    + delta.y, bottom - 1);
    else if (movesize.resize_edges & WLR_EDGE_BOTTOM) bottom = std::max(bottom + delta.y, top    + 1);

    if      (movesize.resize_edges & WLR_EDGE_LEFT)  left  = std::min(left  + delta.x, right - 1);
    else if (movesize.resize_edges & WLR_EDGE_RIGHT) right = std::max(right + delta.x, left  + 1);

    wlr_edges locked_edges = wlr_edges(((movesize.resize_edges & WLR_EDGE_RIGHT)  ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT)
                                     | ((movesize.resize_edges & WLR_EDGE_BOTTOM) ? WLR_EDGE_TOP  : WLR_EDGE_BOTTOM));
    toplevel_set_bounds(toplevel, {
        .x = left,
        .y = top,
        .width  = right  - left,
        .height = bottom - top,
    }, locked_edges);
}

void process_cursor_motion(Server* server, uint32_t time_msecs, wlr_input_device* device, vec2 delta, vec2 rel, vec2 rel_unaccel)
{
    defer {
        update_cursor_debug_visual_position(server);
    };

    // Handle compositor interactions

    if (time_msecs) {
        if (server->interaction_mode == InteractionMode::move) {
            wlr_cursor_move(server->cursor, device, delta.x, delta.y);
            process_cursor_move(server);
            return;
        } else if (server->interaction_mode == InteractionMode::resize) {
            wlr_cursor_move(server->cursor, device, delta.x, delta.y);
            process_cursor_resize(server);
            return;
        } else if (server->interaction_mode == InteractionMode::zone) {
            wlr_cursor_move(server->cursor, device, delta.x, delta.y);
            zone_process_cursor_motion(server);
            return;
        }
    }

    // Get focused surface

    vec2 surface_pos = {};
    wlr_seat* seat = server->seat;
    Surface* surface = nullptr;
    struct wlr_surface* wlr_surface = nullptr;
    if (get_num_pointer_buttons_down(server) > 0 && (surface = Surface::from(seat->pointer_state.focused_surface))) {
        wlr_surface = surface->wlr_surface;
        wlr_box coord_system = surface_get_coord_system(surface);
        surface_pos = get_cursor_pos(server) - vec2(box_origin(coord_system));
    }

    if (!wlr_surface) {
        surface = get_surface_accepting_input_at(server, get_cursor_pos(server), &wlr_surface, &surface_pos);
    }

    // Handle constraints and update mouse

    {
        // log_info("Pointer motion ({:7.2f}, {:7.2f}) rel ({:7.2f}, {:7.2f})\n{}  surface = {}\n{}  pointer surface = {}\n{}  keyboard surface = {}",
        //     dx, dy, rel_dx, rel_dy,
        //     log_indent, surface_to_string(surface),
        //     log_indent, surface_to_string(Surface::from(server->seat->pointer_state.focused_surface)),
        //     log_indent, surface_to_string(Surface::from(server->seat->keyboard_state.focused_surface)));

        if (rel != vec2{} || rel_unaccel != vec2{}) {
            wlr_relative_pointer_manager_v1_send_relative_motion(server->pointer.relative_pointer_manager, server->seat, uint64_t(time_msecs) * 1000, rel.x, rel.y, rel_unaccel.x, rel_unaccel.y);
        }

        bool constraint_active = false;
        defer {
            if (!constraint_active && server->pointer.active_constraint) {
#if NOISY_POINTERS
                log_info("Pointer constraint deactivated: {} (reason: no constraints active)", pointer_constraint_to_string(server->pointer.active_constraint));
#endif
                wlr_pointer_constraint_v1_send_deactivated(server->pointer.active_constraint);
                server->pointer.active_constraint = nullptr;
            }
        };

        Surface* focused_surface = get_focused_surface(server);
        if (focused_surface) {

            wlr_pointer_constraint_v1_type type;
            const pixman_region32_t* region = nullptr;

            if (wlr_pointer_constraint_v1* constraint = wlr_pointer_constraints_v1_constraint_for_surface(server->pointer.pointer_constraints, focused_surface->wlr_surface, server->seat)) {
                constraint_active = true;
                if (constraint != server->pointer.active_constraint) {
                    if (server->pointer.active_constraint) {
#if NOISY_POINTERS
                        log_info("Pointer constraint deactivated: {} (reason: replacing with new constraint)", pointer_constraint_to_string(server->pointer.active_constraint));
                        wlr_pointer_constraint_v1_send_deactivated(server->pointer.active_constraint);
#endif
                    }
#if NOISY_POINTERS
                    log_info("Pointer constraint activated: {}", pointer_constraint_to_string(constraint));
#endif
                    wlr_pointer_constraint_v1_send_activated(constraint);
                    server->pointer.active_constraint = constraint;
                }

                region = &constraint->region;
                type = constraint->type;
            } else if (Toplevel* toplevel = Toplevel::from(focused_surface); toplevel && toplevel->quirks.force_pointer_constraint) {
                region = &toplevel->wlr_surface->input_region;
                type = WLR_POINTER_CONSTRAINT_V1_CONFINED;
            }

            if (region) {
                wlr_box bounds = surface_get_bounds(focused_surface);
                surface_pos = get_cursor_pos(server) - vec2(box_origin(bounds));

                bool was_inside;
                vec2 constrained = constrain_to_region(region, surface_pos, surface_pos + delta, &was_inside);

                // log_warn("constraining ({:.1f}, {:.1f}) to ({}, {}) ({}, {}) = ({:.1f}, {:.1f}), was_inside = {}",
                //     surface_pos.x, surface_pos.y,
                //     bounds.x, bounds.y,
                //     bounds.x + bounds.width, bounds.y + bounds.height,
                //     constrained.x + bounds.x, constrained.y + bounds.y,
                //     was_inside);

                surface = focused_surface;
                wlr_surface = surface->wlr_surface;

                delta = constrained - surface_pos;

                if (!was_inside) {
#if NOISY_POINTERS
                    log_warn("Warping from ({}, {}) to ({}, {})", sx, sy, constrained.x, constrained.y);
#endif

                    wlr_seat_pointer_clear_focus(server->seat);
                    wlr_cursor_warp(server->cursor, nullptr, constrained.x + bounds.x, constrained.y + bounds.y);
                    surface_pos = constrained;
                    delta = {};
                }

                if (type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
                    wlr_seat_pointer_notify_enter(seat, wlr_surface, constrained.x, constrained.y);
                    return;
                }
            }
        }

        wlr_cursor_move(server->cursor, device, delta.x, delta.y);
    }

    // Notify

    if (wlr_surface) {
        if (!time_msecs) {
            timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            time_msecs = now.tv_sec * 1000 + now.tv_nsec / 1000'000;
        }

        wlr_seat_pointer_notify_enter(seat, wlr_surface, surface_pos.x, surface_pos.y);
        wlr_seat_pointer_notify_motion(seat, time_msecs, surface_pos.x, surface_pos.y);
    } else {
        wlr_seat_pointer_notify_clear_focus(seat);
    }

    // Update drag

    seat_drag_update_position(server);
}

static
vec2 pointer_acceleration_apply(Pointer* pointer, const PointerAccelConfig& config, vec2* remainder, vec2 delta)
{
    double speed = glm::length(delta);
    vec2 sens = vec2(config.multiplier * (1 + (std::max(speed, config.offset) - config.offset) * config.rate));

    vec2 new_delta = sens * delta;

    *remainder += new_delta;
    vec2 integer_delta = round_to_zero(*remainder);
    *remainder -= integer_delta;

    if (pointer->server->pointer.debug_accel_rate) {
        log_trace("speed ({:7.2f}, {:7.2f}) ({:7.2f}) -> ({:7.2f}, {:7.2f}) | output ({:7.2f}, {:7.2f}), rem = ({:7.2f}, {:7.2f})",
            delta.x, delta.y, speed, sens.x, sens.y, integer_delta.x, integer_delta.y, remainder->x, remainder->y);
    }

    return integer_delta;
}

void cursor_motion(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_pointer_motion_event* event = static_cast<wlr_pointer_motion_event*>(data);

    Pointer* pointer = Pointer::from(event->pointer);

    vec2 base = { event->delta_x, event->delta_y };
    vec2 accel     = pointer_acceleration_apply(pointer, pointer_accel,     &pointer->accel_remainder,     base);
    vec2 rel_accel = pointer_acceleration_apply(pointer, pointer_rel_accel, &pointer->rel_accel_remainder, base);

    process_cursor_motion(server, event->time_msec, &event->pointer->base, accel, rel_accel, rel_accel);
}

void cursor_motion_absolute(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_pointer_motion_absolute_event* event = static_cast<wlr_pointer_motion_absolute_event*>(data);

    vec2 layout_pos;
    if (event->pointer->output_name) {
        wlr_output_layout_output* layout_output;
        wl_list_for_each(layout_output, &server->output_layout->outputs, link) {
            if (strcmp(layout_output->output->name, event->pointer->output_name) == 0) {
                layout_pos = vec2{layout_output->x, layout_output->y} + vec2{layout_output->output->width, layout_output->output->height} * vec2{event->x, event->y};
                break;
            }
        }
    } else {
        // TODO: *can* output_name be null?
        wlr_cursor_absolute_to_layout_coords(server->cursor, &event->pointer->base, event->x, event->y, &layout_pos.x, &layout_pos.y);
    }

    Pointer* pointer = Pointer::from(event->pointer);

    vec2 delta = layout_pos - get_cursor_pos(server);
    vec2 rel = (layout_pos - pointer->last_abs_pos) * pointer_abs_to_rel_speed_multiplier;
    pointer->last_abs_pos = layout_pos;
    process_cursor_motion(server, event->time_msec, &event->pointer->base, delta, rel, rel);
}

void cursor_button(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_pointer_button_event* event = static_cast<wlr_pointer_button_event*>(data);

    if (input_handle_button(server, *event)) {
        return;
    }

    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

void cursor_axis(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_pointer_axis_event* event = static_cast<wlr_pointer_axis_event*>(data);

    if (input_handle_axis(server, *event)) return;

    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta, event->delta_discrete, event->source, event->relative_direction);
}

void cursor_frame(wl_listener* listener, void*)
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

    Bind input_action = { get_modifiers(server), sym, event.state == WL_KEYBOARD_KEY_STATE_RELEASED };

    // VT Switching

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED && server->wlr_session && sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12) {
        log_debug("Switching to TTY {}", 1 + sym - XKB_KEY_XF86Switch_VT_1);
        wlr_session_change_vt(server->wlr_session, 1 + sym - XKB_KEY_XF86Switch_VT_1);
        return true;
    }

    // User binds

    if (bind_trigger(server, input_action)) {
        return state == WL_KEYBOARD_KEY_STATE_PRESSED;
    }

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED && check_mods(server, Modifiers::Mod)) {

        // Core binds

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
            case XKB_KEY_s:
                surface_unfocus(get_focused_surface(server));
                return true;
            case XKB_KEY_q:
                if (Toplevel* focused = Toplevel::from(get_focused_surface(server))) {
                    toplevel_close(focused);
                }
                return true;
            case XKB_KEY_f:
                if (Toplevel* focused = Toplevel::from(get_focused_surface(server))) {
                    toplevel_set_fullscreen(focused, !toplevel_is_fullscreen(focused), nullptr);
                }
                return true;
            default:
                ;
        }
    }

    if (state == WL_KEYBOARD_KEY_STATE_RELEASED && server->interaction_mode == InteractionMode::focus_cycle) {
        if (sym == server->main_modifier_keysym_left || sym == server->main_modifier_keysym_right) {
            surface_focus(focus_cycle_end(server));
            return true;
        }
    }

    return false;
}

bool input_handle_axis(Server* server, const wlr_pointer_axis_event& event)
{
    // log_trace("Axis {} -> {}", magic_enum::enum_name(event.orientation), event.delta);

    {
        ScrollDirection dir;
        if (event.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
            dir = event.delta > 0 ? ScrollDirection::Down : ScrollDirection::Up;
        } else {
            dir = event.delta >= 0 ? ScrollDirection::Left : ScrollDirection::Right;
        }
        if (bind_trigger(server, Bind { get_modifiers(server), dir })) return true;
    }

    if (check_mods(server, Modifiers::Mod) && event.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
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

    if (event.button == pointer_modifier_button) {
        if (event.state == WL_POINTER_BUTTON_STATE_RELEASED && server->interaction_mode == InteractionMode::focus_cycle) {
            surface_focus(focus_cycle_end(server));
        }
        return true;
    }

    // Handle interrupt focus cycle

    if (event.state == WL_POINTER_BUTTON_STATE_PRESSED && server->interaction_mode == InteractionMode::focus_cycle) {
        Toplevel* selected = focus_cycle_end(server);
        if (wlr_box_contains_point(ptr(surface_get_bounds(selected)), get_cursor_pos(server).x, get_cursor_pos(server).y)) {
            surface_focus(selected);
        }
        return true;
    }

    vec2 surface_pos;
    wlr_surface* surface = nullptr;
    Surface* surface_under_cursor = get_surface_accepting_input_at(server, get_cursor_pos(server), &surface, &surface_pos);

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

    if (Toplevel* toplevel = Toplevel::from(surface_under_cursor); toplevel && check_mods(server, Modifiers::Mod) && server->interaction_mode == InteractionMode::passthrough) {
        if (is_cursor_visible(server)) {
            if (event.button == BTN_LEFT && check_mods(server, Modifiers::Shift)) {
                toplevel_begin_interactive(toplevel, InteractionMode::move);
            } else if (event.button == BTN_RIGHT) {
                toplevel_begin_interactive(toplevel, InteractionMode::resize);
            } else if (event.button == BTN_MIDDLE) {
                toplevel_close(toplevel);
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
            if (prev_focus != surface_under_cursor && server->seat->pointer_state.grab == server->seat->pointer_state.default_grab) {
                surface_focus(surface_under_cursor);
                if (!is_cursor_visible(server)) {
                    log_warn("Button press event suppressed (reason: pointer hidden after moving focus to new window)");
                    return true;
                }
                if (Toplevel* new_focus = Toplevel::from(get_focused_surface(server)); new_focus && new_focus->quirks.force_pointer_constraint) {
                    log_warn("Button press event suppressed (reason: focused moved to window with pointer constraint quirk)");
                    return true;
                }
            }
        } else if (get_focused_surface(server)) {
            surface_unfocus(get_focused_surface(server));
        }
    }

    return false;
}
