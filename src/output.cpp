#include "pch.hpp"
#include "core.hpp"

Output* get_primary_output(Server* server)
{
    Output* first_output = nullptr;
    for (Output* output : server->outputs) {
        if (output->config.disabled) continue;
        if (!first_output) first_output = output;
        if (output->config.primary) return output;
    }
    return first_output;
}

bool output_filter_global(Server* server, Client* client, const wl_global* global)
{
    if (client->is_output_aware) return true;

    Output* output = Output::from(static_cast<struct wlr_output*>(wl_global_get_user_data(global)));

    if (output) {
        if (output == get_primary_output(server)) {
            log_debug("Passing output [{}] to {}", output->wlr_output->name, client_to_string(client));
            return true;
        }
        log_warn("Hiding output [{}] from {}", output->wlr_output->name, client_to_string(client));
        return false;
    } else {
        return true;
    }
}

void update_output_states(Server* server)
{
    // First, remove all outputs from layout. This destroys all output globals
    //  and forces new globals to be re-filtered, so that primary output changes
    //  are made effective
    // TODO: Do this *only* for outputs that are no longer primary outputs

    for (Output* output : server->outputs) {
        if (output->scene_output) {
            wlr_scene_output_destroy(output->scene_output);
            output->scene_output = nullptr;
        }
        if (output->layout_output) {
            wlr_output_layout_remove(server->output_layout, output->wlr_output);
            output->layout_output = nullptr;
        }
        if (output->config.disabled && output->wlr_output->enabled) {
            log_warn("Disabling output: {}", output->wlr_output->name);
            wlr_output_state state;
            wlr_output_state_init(&state);
            wlr_output_state_set_enabled(&state, false);
            wlr_output_commit_state(output->wlr_output, &state);
            wlr_output_state_finish(&state);
        }
    }

    for (Output* output : server->outputs) {
        if (output->config.disabled) continue;

        if (!output->wlr_output->enabled) {
            log_warn("Enabling output: {}", output->wlr_output->name);
            wlr_output_state state;
            wlr_output_state_init(&state);
            wlr_output_state_set_enabled(&state, true);
            wlr_output_commit_state(output->wlr_output, &state);
            wlr_output_state_finish(&state);
        }

        output->layout_output = output->config.pos
            ? wlr_output_layout_add(server->output_layout, output->wlr_output, output->config.pos->x, output->config.pos->y)
            : wlr_output_layout_add_auto(server->output_layout, output->wlr_output);
        output->scene_output = wlr_scene_output_create(server->scene, output->wlr_output);
        wlr_scene_output_layout_add_output(server->scene_output_layout, output->layout_output, output->scene_output);
    }
}

Output* get_output_at(Server* server, vec2 point)
{
    return Output::from(wlr_output_layout_output_at(server->output_layout, point.x, point.y));
}

Output* get_nearest_output_to_point(Server* server, vec2 point)
{
    double closest_distance = INFINITY;
    wlr_output* closest = nullptr;

    wlr_output_layout_output* layout_output;
    wl_list_for_each(layout_output, &server->output_layout->outputs, link) {
        wlr_box box;
        wlr_output_layout_get_box(server->output_layout, layout_output->output, &box);
        vec2 on_output;
        wlr_box_closest_point(&box, point.x, point.y, &on_output.x, &on_output.y);

        double distance = glm::distance(point, on_output);
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
    if (surface->role == SurfaceRole::layer_surface) {
        wlr_output_layout_output* layout_output;
        wl_list_for_each(layout_output, &surface->server->output_layout->outputs, link) {
            Output* output = Output::from(layout_output->output);
            for (zwlr_layer_shell_v1_layer layer : output->layers.enum_values) {
                for (LayerSurface* ls : output->layers[layer]) {
                    if (ls == surface) return output;
                }
            }
        }
        return nullptr;
    }

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
    Output* output = listener_userdata<Output*>(listener);
    wlr_scene* scene = output->server->scene;

    wlr_scene_output* scene_output = wlr_scene_get_scene_output(scene, output->wlr_output);

    wlr_scene_output_commit(scene_output, nullptr);

    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);

    if (output->report_stats) {
        output->frame_reporter.frame(output->wlr_output->name);
    }
}

void output_request_state(wl_listener* listener, void* data)
{
    // This function is called when the backend requests a new state for the output.

    Output* output = listener_userdata<Output*>(listener);
    const wlr_output_event_request_state* event = static_cast<wlr_output_event_request_state*>(data);

    wlr_output_commit_state(output->wlr_output, event->state);
}

void output_destroy(wl_listener* listener, void*)
{
    Output* output = listener_userdata<Output*>(listener);

    log_info("Output [{}] destroyed", output->wlr_output->name);

    wlr_scene_node_destroy(&output->background->node);

    for (zwlr_layer_shell_v1_layer layer : output->layers.enum_values) {
        for (LayerSurface* layer_surface : output->layers[layer]) {
            wlr_layer_surface_v1_destroy(layer_surface->wlr_layer_surface());
        }
    }

    output->wlr_output->data = nullptr;

    std::erase(output->server->outputs, output);

    delete output;
}

void output_new(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_output* wlr_output = static_cast<struct wlr_output*>(data);

    Output* output = new Output{};
    output->wlr_output = wlr_output;
    output->server = server;

    wlr_output->data = output;

    server->outputs.emplace_back(output);

    for (const OutputRule& rule : output_rules) {
        if (std::string_view(rule.name) == wlr_output->name) {
            output->config = rule.config;
            break;
        }
    }

    log_info("Output [{}] added", wlr_output->name);

    if (output->config.pos) {
        log_info("  position = {}", glm::to_string(*output->config.pos));
    } else {
        log_info("  position = auto");
    }
    if (output->config.primary) {
        log_info("  rule.primary = true");
    }

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, !output->config.disabled);

    wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
    if (mode != nullptr) {
        wlr_output_state_set_mode(&state, mode);
    }

    if (wlr_output->adaptive_sync_supported) {
        log_trace("Requesting adaptive sync for {}", wlr_output->description);
        wlr_output_state_set_adaptive_sync_enabled(&state, true);
    } else {
        log_trace("Adaptive sync not supported for {}", wlr_output->description);
    }

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    output->listeners.listen(&wlr_output->events.frame,         output, output_frame);
    output->listeners.listen(&wlr_output->events.request_state, output, output_request_state);
    output->listeners.listen(&wlr_output->events.destroy,       output, output_destroy);

    log_warn("  Adaptive sync: {}", wlr_output->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED);

    output->background = wlr_scene_rect_create(server->layers[Strata::background], wlr_output->width, wlr_output->height, color_to_wlroots(background_color));

    update_output_states(server);

    if (output->config.primary && !server->session.is_nested) {
        wlr_box bounds = output_get_bounds(output);
        vec2 pos = vec2(box_origin(bounds)) + vec2(box_extent(bounds)) / 2.0;
        log_info("Output is primary, warping cursor to center ({}, {})", pos.x, pos.y);
        wlr_cursor_warp(server->cursor, nullptr, pos.x, pos.y);
    }
}

void output_layout_change(wl_listener* listener, void*)
{
    Server* server = listener_userdata<Server*>(listener);

    // TODO: Handled output removal, addition

    wlr_output_layout_output* layout_output;
    wl_list_for_each(layout_output, &server->output_layout->outputs, link) {
        output_reconfigure(Output::from(layout_output->output));
    }
}

// -----------------------------------------------------------------------------

void output_reconfigure(Output* output)
{
    if (!output) return;

    output->workarea = output_get_bounds(output);

    wlr_scene_node_set_position(&output->background->node, output->workarea.x, output->workarea.y);
    wlr_scene_rect_set_size(output->background, output->workarea.width, output->workarea.height);

    for (zwlr_layer_shell_v1_layer layer : output->layers.enum_values) {
        output_layout_layer(output, layer);
    }
}
