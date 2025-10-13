#include "core.hpp"

#include <libevdev/libevdev.h>

#include <optional>

void qz_zone_init(qz_server* server)
{
    server->zone.selector = wlr_scene_rect_create(&server->scene->tree, 0, 0, qz_zone_color_inital.values);
    wlr_scene_node_set_enabled(&server->zone.selector->node, false);
}

bool qz_zone_process_cursor_button(qz_server* server, wlr_pointer_button_event* event)
{
    bool pressed = event->state == WL_POINTER_BUTTON_STATE_PRESSED;

    // Consolidate all interaction state

    if (event->button == BTN_LEFT) {
        if (pressed && qz_is_main_mod_down(server) && !(qz_get_modifiers(server) & WLR_MODIFIER_SHIFT)) {
            double sx, sy;
            wlr_surface* surface = nullptr;
            qz_toplevel* toplevel = qz_get_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
            if (toplevel) {
                qz_toplevel_focus(toplevel);

                if (qz_toplevel_is_interactable(toplevel)) {
                    wlr_scene_rect_set_color(server->zone.selector, qz_zone_color_inital.values);
                    wlr_scene_node_set_enabled(&server->zone.selector->node, true);
                    server->zone.selecting = false;
                    server->zone.moving = true;
                    server->cursor_mode = qz_cursor_mode::zone;
                    qz_zone_process_cursor_motion(server);
                }
            }
            return true;
        } else if (server->zone.moving) {
            if (server->zone.selecting) {
                if (server->focused_toplevel) {
                    wlr_box box = qz_box_round_to_wlr_box(server->zone.final_zone);
                    qz_toplevel_set_bounds(server->focused_toplevel, box);
                }
            }
            wlr_scene_node_set_enabled(&server->zone.selector->node, false);
            server->cursor_mode = qz_cursor_mode::passthrough;
            server->zone.moving = false;
            return pressed;
        }
    }
    else if (event->button == BTN_RIGHT && server->zone.moving) {
        if (pressed) {
            server->zone.selecting = !server->zone.selecting;
            wlr_scene_rect_set_color(server->zone.selector, server->zone.selecting
                                                                ? qz_zone_color_select.values
                                                                : qz_zone_color_inital.values);
        }
        return true;
    }

    return false;
}

void qz_zone_process_cursor_motion(qz_server* server)
{
    auto output = qz_get_output_at(server, server->cursor->x, server->cursor->y);
    auto bounds = qz_output_get_bounds(output);

    qz_point point{ server->cursor->x, server->cursor->y };

    qz_box pointer_zone = {};
    bool any_zones = false;

    auto extent = qz_point(double(bounds.width) / qz_zone_horizontal_zones, double(bounds.height) / qz_zone_vertical_zones);
    for (uint32_t zone_x = 0; zone_x < qz_zone_horizontal_zones; ++zone_x) {
        for (uint32_t zone_y = 0; zone_y < qz_zone_vertical_zones; ++zone_y) {
            auto rect = qz_box {
                .x = bounds.x + extent.x * zone_x,
                .y = bounds.y + extent.y * zone_y,
                .width = extent.x,
                .height = extent.y,
            };

            auto check_rect = rect;
            check_rect.x -= qz_zone_zone_selection_leeway.x;
            check_rect.y -= qz_zone_zone_selection_leeway.y;
            check_rect.width += qz_zone_zone_selection_leeway.x * 2;
            check_rect.height += qz_zone_zone_selection_leeway.y * 2;

            if (qz_box_contains_point(check_rect, point)) {

                // Compute padding
                constexpr auto pad = [](int i, bool c) { return c ? qz_zone_external_padding_ltrb[i] : qz_zone_internal_padding / 2; };
                qz_point tl_inset{ pad(0, zone_x == 0),                      pad(1, zone_y == 0)                    };
                qz_point br_inset{ pad(2, zone_x == (qz_zone_horizontal_zones - 1)), pad(3, zone_y == (qz_zone_vertical_zones - 1)) };
                rect.x += tl_inset.x;
                rect.y += tl_inset.y;
                rect.width  -= tl_inset.x + br_inset.x;
                rect.height -= tl_inset.y + br_inset.y;

                // Expand selection zones
                pointer_zone = any_zones ? qz_box_outer(pointer_zone, rect) : rect;
                any_zones = true;
            }
        }
    }

    if (any_zones) {
        if (server->zone.selecting) {
            server->zone.final_zone = qz_box_outer(server->zone.initial_zone, pointer_zone);
        } else {
            server->zone.final_zone = server->zone.initial_zone = pointer_zone;
        }

        auto b = qz_box_round_to_wlr_box(server->zone.final_zone);

        wlr_scene_rect_set_size(server->zone.selector, b.width, b.height);
        wlr_scene_node_set_position(&server->zone.selector->node, b.x, b.y);
        // TODO: Put in top-most layer
        wlr_scene_node_raise_to_top(&server->zone.selector->node);
    }
}

void qz_zone_begin_selection(qz_server* server)
{
    server->cursor_mode = qz_cursor_mode::zone;

    qz_zone_process_cursor_motion(server);
}

void qz_zone_end_selection(qz_server* server)
{
    server->cursor_mode = qz_cursor_mode::passthrough;
    wlr_scene_node_set_enabled(&server->zone.selector->node, false);
}
