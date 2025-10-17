#include "pch.hpp"
#include "core.hpp"

Output* get_output_at(Server* server, Point point)
{
    return Output::from(wlr_output_layout_output_at(server->output_layout, point.x, point.y));
}

Output* get_nearest_output_to_point(Server* server, Point point)
{
    double closest_distance = INFINITY;
    wlr_output* closest = nullptr;

    wlr_output_layout_output* layout_output;
    wl_list_for_each(layout_output, &server->output_layout->outputs, link) {
        wlr_box box;
        wlr_output_layout_get_box(server->output_layout, layout_output->output, &box);
        Point on_output;
        wlr_box_closest_point(&box, point.x, point.y, &on_output.x, &on_output.y);

        double distance = distance_between(point, on_output);
        if (distance < closest_distance) {
            closest_distance = distance;
            closest = layout_output->output;
        }
        if (distance == 0) break;
    }

    return Output::from(closest);
}

Output* get_nearest_output_to_box(Server* server, wlr_box box)
{
    return get_nearest_output_to_point(server, { box.x + box.width  / 2.0, box.y + box.height / 2.0 });
}

Output* get_output_for_surface(Surface* surface)
{
    return get_nearest_output_to_box(surface->server, surface_get_bounds(surface));
}

wlr_box output_get_bounds(Output* output)
{
    wlr_box box;
    wlr_output_layout_get_box(output->server->output_layout, output->wlr_output, &box);
    return box;
}

void output_frame(wl_listener* listener, void*)
{
    // This function is called every time an output is ready to display a frame, generally at the output's refresh rate (e.g. 60Hz)

    Output* output = listener_userdata<Output*>(listener);
    wlr_scene* scene = output->server->scene;

    wlr_scene_output* scene_output = wlr_scene_get_scene_output(scene, output->wlr_output);

    wlr_scene_output_commit(scene_output, nullptr);

    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

void output_request_state(wl_listener* listener, void* data)
{
    // This function is called when the backend requests a new state for the output.
    // For example, wayland and X11 backends request a new mode when the output window is resized

    Output* output = listener_userdata<Output*>(listener);
    const wlr_output_event_request_state* event = static_cast<wlr_output_event_request_state*>(data);

    wlr_output_commit_state(output->wlr_output, event->state);
}

void output_destroy(wl_listener* listener, void*)
{
    Output* output = listener_userdata<Output*>(listener);

    wlr_scene_node_destroy(&output->background->node);

    delete output;
}

void server_new_output(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_output* wlr_output = static_cast<struct wlr_output*>(data);

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
    if (mode != nullptr) {
        wlr_output_state_set_mode(&state, mode);
    }

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    Output* output = new Output{};
    output->wlr_output = wlr_output;
    output->server = server;

    wlr_output->data = output;

    output->listeners.listen(&wlr_output->events.frame,         output, output_frame);
    output->listeners.listen(&wlr_output->events.request_state, output, output_request_state);
    output->listeners.listen(&wlr_output->events.destroy,       output, output_destroy);

    const OutputRule* matched_rule = nullptr;
    for (const OutputRule& rule : output_rules) {
        if (std::string_view(rule.name) == wlr_output->name) {
            matched_rule = &rule;
            break;
        }
    }

    if (matched_rule) {
        log_info("Output [{}] matched rule. x = {}, y = {}", wlr_output->name, matched_rule->x, matched_rule->y);
    } else {
        log_info("Output [{}] matches no rules, using auto layout", wlr_output->name);
    }

    output->background = wlr_scene_rect_create(server->layers(Strata::background), wlr_output->width, wlr_output->height, background_color.values);

    wlr_output_layout_output* l_output = matched_rule
        ? wlr_output_layout_add(server->output_layout, wlr_output, matched_rule->x, matched_rule->y)
        : wlr_output_layout_add_auto(server->output_layout, wlr_output);
    wlr_scene_output* scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_output_layout, l_output, scene_output);
    output->scene_output = scene_output;
}

void server_output_layout_change(wl_listener* listener, void*)
{
    Server* server = listener_userdata<Server*>(listener);

    // TODO: Handled output removal, addition

    // TODO: On output removal, reparent or close orphaned LayerSurfece elements

    wlr_output_layout_output* layout_output;
    wl_list_for_each(layout_output, &server->output_layout->outputs, link) {
        output_reconfigure(Output::from(layout_output->output));
    }
}

// -----------------------------------------------------------------------------

void output_reconfigure(Output* output)
{
    output->workarea = output_get_bounds(output);

    wlr_scene_node_set_position(&output->background->node, output->workarea.x, output->workarea.y);
    wlr_scene_rect_set_size(output->background, output->workarea.width, output->workarea.height);

    for (uint32_t i = uint32_t(Output::zwlr_layer_shell_v1_layer_count); i-- > 0;) {
        output_layout_layer(output, zwlr_layer_shell_v1_layer(i));
    }
}
