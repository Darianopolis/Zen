#include "pch.hpp"
#include "core.hpp"

wlr_output_layout_output* Output::layout_output() const
{
    wlr_output_layout_output* layout_output;
    wl_list_for_each(layout_output, &server->output_layout->outputs, link) {
        if (layout_output->output == wlr_output) {
            return layout_output;
        }
    }
    return nullptr;
}

static
void output_layout_report_configuration(Server* server)
{
    wlr_output_configuration_v1* config = wlr_output_configuration_v1_create();

    for (Output* output : server->outputs) {
        auto* head = wlr_output_configuration_head_v1_create(config, output->wlr_output);
        if (auto* layout_output = output->layout_output()) {
            head->state.x = layout_output->x;
            head->state.y = layout_output->y;
        }
    }

    wlr_output_manager_v1_set_configuration(server->output_manager, config);
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
        for (Output* output : surface->server->outputs) {
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

    wlr_scene_output* scene_output = output->scene_output();

    wlr_scene_output_commit(scene_output, nullptr);

    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
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

    wlr_scene_node_destroy(&output->background_base->node);
    wlr_scene_node_destroy(&output->background_color->node);
    background_output_destroy(output);

    for (zwlr_layer_shell_v1_layer layer : output->layers.enum_values) {
        for (LayerSurface* layer_surface : output->layers[layer]) {
            wlr_layer_surface_v1_destroy(layer_surface->wlr_layer_surface());
        }
    }

    output->wlr_output->data = nullptr;

    for (Surface* surface : output->server->surfaces) {
        std::erase(surface->current_outputs, output);
    }

    std::erase(output->server->outputs, output);

    output->server->script.on_output_add_or_remove(output, false);

    scene_reconfigure(output->server);

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

    log_info("Output [{}] added", wlr_output->name);

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    {
        wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, true);

        if (wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output)) {
            wlr_output_state_set_mode(&state, mode);
        }

        if (wlr_output->adaptive_sync_supported) {
            wlr_output_state_set_adaptive_sync_enabled(&state, true);
        }

        wlr_output_commit_state(wlr_output, &state);
        wlr_output_state_finish(&state);
    }

    output->listeners.listen(&wlr_output->events.frame,         output, output_frame);
    output->listeners.listen(&wlr_output->events.request_state, output, output_request_state);
    output->listeners.listen(&wlr_output->events.destroy,       output, output_destroy);

    output->background_base  = wlr_scene_rect_create(server->layers[Strata::background], wlr_output->width, wlr_output->height, color_to_wlroots({1, 0, 1, 1}));
    output->background_color = wlr_scene_rect_create(server->layers[Strata::background], wlr_output->width, wlr_output->height, color_to_wlroots(server->config.layout.background_color));

    background_output_set(output);

    wlr_output_layout_add_auto(server->output_layout, output->wlr_output);

    server->script.on_output_add_or_remove(output, true);
}

void output_layout_change(wl_listener* listener, void*)
{
    Server* server = listener_userdata<Server*>(listener);

    for (Output* output : server->outputs) {
        if (auto* layout_output = output->layout_output()) {
            if (!output->scene_output()) {
                log_warn("Adding output [{}] to scene", output->wlr_output->name);
                auto scene_output = wlr_scene_output_create(server->scene, output->wlr_output);
                wlr_scene_output_layout_add_output(server->scene_output_layout, layout_output, scene_output);
            }
        } else if (wlr_scene_output* scene_output = output->scene_output()) {
            log_warn("Removing output [{}] from scene", output->wlr_output->name);
            wlr_scene_output_destroy(scene_output);
        }
        output_reconfigure(output);
    }

    output_layout_report_configuration(server);
}

// -----------------------------------------------------------------------------

void output_reconfigure(Output* output)
{
    if (!output) return;

    // Set background

    auto* o = output->wlr_output;
    auto* lo = output->layout_output();

    wlr_scene_node_set_position(&output->background_base->node, lo->x, lo->y);
    wlr_scene_rect_set_size(output->background_base, o->width, o->height);

    wlr_scene_node_set_position(&output->background_color->node, lo->x, lo->y);
    wlr_scene_rect_set_size(output->background_color, o->width, o->height);

    background_output_position(output);

    // Update workarea

    output->workarea = output_get_bounds(output);

    auto pad = output->server->config.layout.zone_external_padding;
    output->workarea.x      += pad.left;
    output->workarea.y      += pad.top;
    output->workarea.width  -= pad.left + pad.right;
    output->workarea.height -= pad.top + pad.bottom;

    for (zwlr_layer_shell_v1_layer layer : output->layers.enum_values) {
        output_reconfigure_layer(output, layer);
    }
}

void outputs_reconfigure_all(Server* server)
{
    for (auto* output : server->outputs) {
        output_reconfigure(output);
    }
}

// -----------------------------------------------------------------------------

static
void output_manager_apply_or_test(Server* server, wlr_output_configuration_v1* config, bool test)
{
    defer { wlr_output_configuration_v1_destroy(config); };
    bool ok = true;

    wlr_output_configuration_head_v1* head;
    wl_list_for_each(head, &config->heads, link) {
        Output* output = Output::from(head->state.output);

        wlr_output_state state;
        defer { wlr_output_state_finish(&state); };
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, head->state.enabled);
        if (head->state.enabled) {
            if (head->state.mode) {
                wlr_output_state_set_mode(&state, head->state.mode);
            } else {
                wlr_output_state_set_custom_mode(&state,
                    head->state.custom_mode.width,
                    head->state.custom_mode.height,
                    head->state.custom_mode.refresh);
            }

            wlr_output_state_set_transform(&state, head->state.transform);
            wlr_output_state_set_scale(&state, head->state.scale);
            wlr_output_state_set_adaptive_sync_enabled(&state, head->state.adaptive_sync_enabled);

            if (!test) {
                auto* layout_output = output->layout_output();
                if (!layout_output || head->state.x != layout_output->x || head->state.y != layout_output->y) {
                    // TODO: We want to preserve "auto" layout status when re-enabling outputs
                    wlr_output_layout_add(output->server->output_layout, output->wlr_output, head->state.x, head->state.y);
                }
            }
        }

        ok &= test ? wlr_output_test_state(  output->wlr_output, &state)
                   : wlr_output_commit_state(output->wlr_output, &state);

        if (!test && !head->state.enabled) {
            wlr_output_layout_remove(server->output_layout, output->wlr_output);
        }
    }

    if (ok) wlr_output_configuration_v1_send_succeeded(config);
    else    wlr_output_configuration_v1_send_failed(   config);
}

void output_manager_apply(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_output_configuration_v1* config = static_cast<wlr_output_configuration_v1*>(data);
    output_manager_apply_or_test(server, config, false);
}

void output_manager_test( wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    wlr_output_configuration_v1* config = static_cast<wlr_output_configuration_v1*>(data);
    output_manager_apply_or_test(server, config, true);
}
