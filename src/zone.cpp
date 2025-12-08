#include "pch.hpp"
#include "core.hpp"

void zone_init(Server* server)
{
    auto& c = server->config.layout;
    server->zone.selector = wlr_scene_rect_create(server->layers[Strata::overlay], 0, 0, color_to_wlroots(c.zone_color_inital));
    wlr_scene_node_set_enabled(&server->zone.selector->node, false);
}

bool zone_process_cursor_button(Server* server, const wlr_pointer_button_event& event)
{
    bool pressed = event.state == WL_POINTER_BUTTON_STATE_PRESSED;

    // Consolidate all interaction state

    auto& c = server->config.layout;

    if (event.button == BTN_LEFT) {
        if (pressed && check_mods(server, Modifiers::Mod) && !check_mods(server, Modifiers::Shift)) {
            if (is_cursor_visible(server)) {
                vec2 surface_pos;
                wlr_surface* surface = nullptr;
                if (Toplevel* toplevel = Toplevel::from(get_surface_accepting_input_at(server, get_cursor_pos(server), &surface, &surface_pos))) {
                    server->zone.toplevel = weak_from(toplevel);

                    if (toplevel_is_interactable(toplevel)) {
                        wlr_scene_rect_set_color(server->zone.selector, color_to_wlroots(c.zone_color_inital));
                        wlr_scene_node_set_enabled(&server->zone.selector->node, true);
                        server->zone.selecting = false;
                        set_interaction_mode(server, InteractionMode::zone);
                        zone_process_cursor_motion(server);
                    }
                }
            } else {
                log_warn("Tried to initiate zone interaction but cursor not visible");
            }
            return true;
        } else if (server->interaction_mode == InteractionMode::zone) {
            if (server->zone.selecting) {
                if (Toplevel* toplevel = server->zone.toplevel.get()) {
                    toplevel_set_bounds(toplevel, server->zone.final_zone, BoundsType::normal);
                    surface_try_focus(server, toplevel);
                }
            }
            wlr_scene_node_set_enabled(&server->zone.selector->node, false);
            set_interaction_mode(server, InteractionMode::passthrough);
            return true;
        }
    }
    else if (event.button == BTN_RIGHT && server->interaction_mode == InteractionMode::zone) {
        if (pressed) {
            server->zone.selecting = !server->zone.selecting;
            wlr_scene_rect_set_color(server->zone.selector, server->zone.selecting
                                                                ? color_to_wlroots(c.zone_color_select)
                                                                : color_to_wlroots(c.zone_color_inital));
        }
        return true;
    }

    return false;
}

void get_zone_axis(i32 start, i32 total_length, i32 inner_pad, i32 num_zones, i32 i, i32* offset, i32* size)
{
    i32 usable_length = total_length - (inner_pad * (num_zones - 1));
    f64 ideal_zone_size = f64(usable_length) / num_zones;
    *offset  = std::round(ideal_zone_size *  i     );
    *size    = std::round(ideal_zone_size * (i + 1)) - *offset;
    *offset += start + inner_pad * i;
}

wlr_box get_zone_box(Server* server, wlr_box workarea, i32 zone_x, i32 zone_y)
{
    auto& c = server->config.layout;

    wlr_box zone;
    get_zone_axis(workarea.x, workarea.width,  c.zone_internal_padding, c.zone_horizontal_zones, zone_x, &zone.x, &zone.width);
    get_zone_axis(workarea.y, workarea.height, c.zone_internal_padding, c.zone_vertical_zones,   zone_y, &zone.y, &zone.height);
    return zone;
}

void zone_process_cursor_motion(Server* server)
{
    vec2 point = get_cursor_pos(server);
    Output* output = get_output_at(server, point);
    wlr_box workarea = output->workarea;

    wlr_box pointer_zone = {};
    bool any_zones = false;

    auto& c = server->config.layout;

    for (u32 zone_x = 0; zone_x < c.zone_horizontal_zones; ++zone_x) {
        for (u32 zone_y = 0; zone_y < c.zone_vertical_zones; ++zone_y) {
            wlr_box rect = get_zone_box(server, workarea, zone_x, zone_y);
            wlr_box check_rect {
                .x      = rect.x      - c.zone_selection_leeway.x,
                .y      = rect.y      - c.zone_selection_leeway.y,
                .width  = rect.width  + c.zone_selection_leeway.x * 2,
                .height = rect.height + c.zone_selection_leeway.y * 2,
            };

            if (wlr_box_contains_point(&check_rect, point.x, point.y)) {
                pointer_zone = any_zones ? box_outer(pointer_zone, rect) : rect;
                any_zones = true;
            }
        }
    }

    if (any_zones) {
        if (server->zone.selecting) {
            server->zone.final_zone = box_outer(server->zone.initial_zone, pointer_zone);
        } else {
            server->zone.final_zone = server->zone.initial_zone = pointer_zone;
        }

        wlr_box b = server->zone.final_zone;

        wlr_scene_node_set_enabled(&server->zone.selector->node, true);
        wlr_scene_rect_set_size(server->zone.selector, b.width, b.height);
        wlr_scene_node_set_position(&server->zone.selector->node, b.x, b.y);
    } else {
        wlr_scene_node_set_enabled(&server->zone.selector->node, false);
    }
}

void zone_end_selection(Server* server)
{
    if (server->interaction_mode != InteractionMode::zone) return;
    wlr_scene_node_set_enabled(&server->zone.selector->node, false);
    server->interaction_mode = InteractionMode::passthrough;
}
