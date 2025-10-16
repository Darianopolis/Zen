#include "pch.hpp"
#include "core.hpp"

wlr_box zone_apply_external_padding(wlr_box box)
{
    auto pad = zone_external_padding_ltrb;

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
    server->zone.selector = wlr_scene_rect_create(&server->scene->tree, 0, 0, zone_color_inital.values);
    wlr_scene_node_set_enabled(&server->zone.selector->node, false);
}

bool zone_process_cursor_button(Server* server, wlr_pointer_button_event* event)
{
    bool pressed = event->state == WL_POINTER_BUTTON_STATE_PRESSED;

    // Consolidate all interaction state

    if (event->button == BTN_LEFT) {
        if (pressed && is_main_mod_down(server) && !(get_modifiers(server) & WLR_MODIFIER_SHIFT)) {
            double sx, sy;
            wlr_surface* surface = nullptr;
            Toplevel* toplevel = get_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
            if (toplevel) {
                toplevel_focus(toplevel);

                if (toplevel_is_interactable(toplevel)) {
                    wlr_scene_rect_set_color(server->zone.selector, zone_color_inital.values);
                    wlr_scene_node_set_enabled(&server->zone.selector->node, true);
                    server->zone.selecting = false;
                    server->zone.moving = true;
                    server->cursor_mode = CursorMode::zone;
                    zone_process_cursor_motion(server);
                }
            }
            return true;
        } else if (server->zone.moving) {
            if (server->zone.selecting) {
                if (get_focused_toplevel(server)) {
                    wlr_box box = box_round_to_wlr_box(server->zone.final_zone);
                    toplevel_set_bounds(get_focused_toplevel(server), box);
                }
            }
            wlr_scene_node_set_enabled(&server->zone.selector->node, false);
            server->cursor_mode = CursorMode::passthrough;
            server->zone.moving = false;
            return true;
        }
    }
    else if (event->button == BTN_RIGHT && server->zone.moving) {
        if (pressed) {
            server->zone.selecting = !server->zone.selecting;
            wlr_scene_rect_set_color(server->zone.selector, server->zone.selecting
                                                                ? zone_color_select.values
                                                                : zone_color_inital.values);
        }
        return true;
    }

    return false;
}

void zone_process_cursor_motion(Server* server)
{
    Output* output = get_output_at(server, {server->cursor->x, server->cursor->y});
    wlr_box bounds = output_get_bounds(output);

    Point point { server->cursor->x, server->cursor->y };

    Box pointer_zone = {};
    bool any_zones = false;

    Point extent { double(bounds.width) / zone_horizontal_zones, double(bounds.height) / zone_vertical_zones };
    for (uint32_t zone_x = 0; zone_x < zone_horizontal_zones; ++zone_x) {
        for (uint32_t zone_y = 0; zone_y < zone_vertical_zones; ++zone_y) {
            Box rect {
                .x = bounds.x + extent.x * zone_x,
                .y = bounds.y + extent.y * zone_y,
                .width = extent.x,
                .height = extent.y,
            };

            Box check_rect {
                .x      = rect.x      - zone_selection_leeway.x,
                .y      = rect.y      - zone_selection_leeway.y,
                .width  = rect.width  + zone_selection_leeway.x * 2,
                .height = rect.height + zone_selection_leeway.y * 2,
            };

            if (box_contains_point(check_rect, point)) {

                auto outer = zone_external_padding_ltrb;

                // Compute padding
                constexpr auto pad = [](int external_padding, bool c) { return c ? external_padding : zone_internal_padding / 2; };
                Point tl_inset{ pad(outer.left,  zone_x == 0),                           pad(outer.top,    zone_y == 0)                         };
                Point br_inset{ pad(outer.right, zone_x == (zone_horizontal_zones - 1)), pad(outer.bottom, zone_y == (zone_vertical_zones - 1)) };
                rect.x += tl_inset.x;
                rect.y += tl_inset.y;
                rect.width  -= tl_inset.x + br_inset.x;
                rect.height -= tl_inset.y + br_inset.y;

                // Expand selection zones
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

        wlr_box b = box_round_to_wlr_box(server->zone.final_zone);

        wlr_scene_rect_set_size(server->zone.selector, b.width, b.height);
        wlr_scene_node_set_position(&server->zone.selector->node, b.x, b.y);
        // TODO: Put in top-most layer
        wlr_scene_node_raise_to_top(&server->zone.selector->node);
    }
}

void zone_begin_selection(Server* server)
{
    server->cursor_mode = CursorMode::zone;

    zone_process_cursor_motion(server);
}

void zone_end_selection(Server* server)
{
    server->cursor_mode = CursorMode::passthrough;
    wlr_scene_node_set_enabled(&server->zone.selector->node, false);
}
