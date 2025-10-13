#include "quartz.hpp"

qz_output* qz_get_output_at(qz_server* server, double x, double y)
{
    wlr_output* o = wlr_output_layout_output_at(server->output_layout, x, y);
    return o ? static_cast<qz_output*>(o->data) : nullptr;
}

qz_output* qz_get_output_for_client(qz_client* client)
{
    wlr_box bounds = qz_client_get_bounds(client);
    int x = bounds.x + bounds.width / 2;
    int y = bounds.y + bounds.height / 2;
    // TODO: - Keep track of the last monitor to be associated with this window
    //       - Check monitor output based on bounds instead of centroid?
    return qz_get_output_at(client->server, x, y);
}

wlr_box qz_output_get_bounds(qz_output* output)
{
    wlr_box box;
    wlr_output_layout_get_box(output->server->output_layout, output->wlr_output, &box);
    return box;
}

void qz_output_frame(wl_listener* listener, void*)
{
    // This function is called every time an output is ready to display a frame, generally at the output's refresh rate (e.g. 60Hz)

    qz_output* output = qz_listener_userdata<qz_output*>(listener);
    wlr_scene* scene = output->server->scene;

    wlr_scene_output* scene_output = wlr_scene_get_scene_output(scene, output->wlr_output);

    wlr_scene_output_commit(scene_output, nullptr);

    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

void qz_output_request_state(wl_listener* listener, void* data)
{
    // This function is called when the backend requests a new state for the output.
    // For example, wayland and X11 backends request a new mode when the output window is resized

    qz_output* output = qz_listener_userdata<qz_output*>(listener);
    const wlr_output_event_request_state* event = static_cast<wlr_output_event_request_state*>(data);

    wlr_output_commit_state(output->wlr_output, event->state);
}

void qz_output_destroy(wl_listener* listener, void*)
{
    qz_output* output = qz_listener_userdata<qz_output*>(listener);

    std::erase(output->server->outputs, output);

    wlr_scene_node_destroy(&output->background->node);

    delete output;
}

void qz_output_reconfigure(qz_output* output)
{
    wlr_box bounds = qz_output_get_bounds(output);

    wlr_scene_node_set_position(&output->background->node, bounds.x, bounds.y);
    wlr_scene_rect_set_size(output->background, bounds.width, bounds.height);
}

void qz_server_new_output(wl_listener* listener, void* data)
{
    // This event is raised by the backend when a new output (aka a display or monitor) becomes available

    qz_server* server = qz_listener_userdata<qz_server*>(listener);
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

    qz_output* output = new qz_output{};
    output->wlr_output = wlr_output;
    output->server = server;

    wlr_output->data = output;

    output->listeners.listen(&wlr_output->events.frame,         output, qz_output_frame);
    output->listeners.listen(&wlr_output->events.request_state, output, qz_output_request_state);
    output->listeners.listen(&wlr_output->events.destroy,       output, qz_output_destroy);

    server->outputs.emplace_back(output);

    const qz_monitor_rule* matched_rule = nullptr;
    for (const qz_monitor_rule& rule : qz_monitor_rules) {
        if (std::string_view(rule.name) == wlr_output->name) {
            matched_rule = &rule;
            break;
        }
    }

    if (matched_rule) {
        wlr_log(WLR_INFO, "Output [%s] matched rule. x = %i, y = %i", wlr_output->name, matched_rule->x, matched_rule->y);
    } else {
        wlr_log(WLR_INFO, "Output [%s] matches no rules, using auto layout", wlr_output->name);
    }

    output->background = wlr_scene_rect_create(&server->scene->tree, wlr_output->width, wlr_output->height, qz_background_color.values);

    wlr_output_layout_output* l_output = matched_rule
        ? wlr_output_layout_add(server->output_layout, wlr_output, matched_rule->x, matched_rule->y)
        : wlr_output_layout_add_auto(server->output_layout, wlr_output);
    wlr_scene_output* scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_output_layout, l_output, scene_output);
    output->scene_output = scene_output;
}

void qz_server_output_layout_change(wl_listener* listener, void*)
{
    qz_server* server = qz_listener_userdata<qz_server*>(listener);

    // TODO: Handled output removal, addition

    for (qz_output* output : server->outputs) {
        qz_output_reconfigure(output);
    }
}
