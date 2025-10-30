#include "pch.hpp"
#include "core.hpp"

wlr_box zone_apply_external_padding(wlr_box box)
{
    auto pad = zone_external_padding;

    if (box.width > pad.left + pad.right) {
        box.x += pad.left;
        box.width -= pad.left + pad.right;
    }
    if (box.height > pad.top + pad.bottom) {
        box.y += pad.top;
        box.height -= pad.top + pad.bottom;
    }
    return box;
}

void zone_init(Server* server)
{
    server->zone.selector = wlr_scene_rect_create(server->layers[Strata::overlay], 0, 0, glm::value_ptr(zone_color_inital));
    wlr_scene_node_set_enabled(&server->zone.selector->node, false);
}

bool zone_process_cursor_button(Server* server, const wlr_pointer_button_event& event)
{
    bool pressed = event.state == WL_POINTER_BUTTON_STATE_PRESSED;

    // Consolidate all interaction state

    if (event.button == BTN_LEFT) {
        if (pressed && is_main_mod_down(server) && !is_mod_down(server, WLR_MODIFIER_SHIFT)) {
            if (is_cursor_visible(server)) {
                vec2 surface_pos;
                wlr_surface* surface = nullptr;
                if (Toplevel* toplevel = Toplevel::from(get_surface_accepting_input_at(server, get_cursor_pos(server), &surface, &surface_pos))) {
                    surface_focus(toplevel);

                    if (toplevel_is_interactable(toplevel)) {
                        wlr_scene_rect_set_color(server->zone.selector, glm::value_ptr(zone_color_inital));
                        wlr_scene_node_set_enabled(&server->zone.selector->node, true);
                        server->zone.selecting = false;
                        server->zone.moving = true;
                        set_interaction_mode(server, InteractionMode::zone);
                        zone_process_cursor_motion(server);
                    }
                }
            } else {
                log_warn("Tried to initiate zone interaction but cursor not visible");
            }
            return true;
        } else if (server->zone.moving) {
            if (server->zone.selecting) {
                if (Toplevel* focused_toplevel = Toplevel::from(get_focused_surface(server))) {
                    toplevel_set_bounds(focused_toplevel, server->zone.final_zone);
                }
            }
            wlr_scene_node_set_enabled(&server->zone.selector->node, false);
            set_interaction_mode(server, InteractionMode::passthrough);
            server->zone.moving = false;
            return true;
        }
    }
    else if (event.button == BTN_RIGHT && server->zone.moving) {
        if (pressed) {
            server->zone.selecting = !server->zone.selecting;
            wlr_scene_rect_set_color(server->zone.selector, server->zone.selecting
                                                                ? glm::value_ptr(zone_color_select)
                                                                : glm::value_ptr(zone_color_inital));
        }
        return true;
    }

    return false;
}

void get_zone_axis(int start, int total_length, int start_pad, int inner_pad, int end_pad, int num_zones, int i, int* offset, int* size)
{
    int usable_length = total_length - start_pad - end_pad - (inner_pad * (num_zones - 1));
    double ideal_zone_size = double(usable_length) / num_zones;
    *offset  = std::round(ideal_zone_size *  i     );
    *size    = std::round(ideal_zone_size * (i + 1)) - *offset;
    *offset += start + start_pad + inner_pad * i;
}

wlr_box get_zone_box(wlr_box workarea, int zone_x, int zone_y)
{
    wlr_box zone;
    get_zone_axis(workarea.x, workarea.width,  zone_external_padding.left, zone_internal_padding, zone_external_padding.right,  zone_horizontal_zones, zone_x, &zone.x, &zone.width);
    get_zone_axis(workarea.y, workarea.height, zone_external_padding.top,  zone_internal_padding, zone_external_padding.bottom, zone_vertical_zones,   zone_y, &zone.y, &zone.height);
    return zone;
}

void zone_process_cursor_motion(Server* server)
{
    vec2 point = get_cursor_pos(server);
    Output* output = get_output_at(server, point);
    wlr_box workarea = output->workarea;

    wlr_box pointer_zone = {};
    bool any_zones = false;

    for (uint32_t zone_x = 0; zone_x < zone_horizontal_zones; ++zone_x) {
        for (uint32_t zone_y = 0; zone_y < zone_vertical_zones; ++zone_y) {
            wlr_box rect = get_zone_box(workarea, zone_x, zone_y);
            wlr_box check_rect {
                .x      = rect.x      - zone_selection_leeway.x,
                .y      = rect.y      - zone_selection_leeway.y,
                .width  = rect.width  + zone_selection_leeway.x * 2,
                .height = rect.height + zone_selection_leeway.y * 2,
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

        wlr_scene_rect_set_size(server->zone.selector, b.width, b.height);
        wlr_scene_node_set_position(&server->zone.selector->node, b.x, b.y);
    }
}

void zone_begin_selection(Server* server)
{
    set_interaction_mode(server, InteractionMode::zone);

    zone_process_cursor_motion(server);
}

void zone_end_selection(Server* server)
{
    if (server->interaction_mode != InteractionMode::zone) return;
    wlr_scene_node_set_enabled(&server->zone.selector->node, false);
    server->interaction_mode = InteractionMode::passthrough;
}
