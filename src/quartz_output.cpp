#include "quartz.hpp"

qz_output* qz_get_output_at(struct qz_server* server, double x, double y)
{
    struct wlr_output* o = wlr_output_layout_output_at(server->output_layout, x, y);
    return o ? static_cast<qz_output*>(o->data) : nullptr;
}

qz_output* qz_get_output_for_toplevel(struct qz_toplevel* toplevel)
{
    auto bounds = qz_toplevel_get_bounds(toplevel);
    int x = bounds.x + bounds.width / 2;
    int y = bounds.y + bounds.height / 2;
    // TODO: - Keep track of the last monitor to be associated with this window
    //       - Check monitor output based on bounds instead of centroid?
    return qz_get_output_at(toplevel->server, x, y);
}

wlr_box qz_output_get_bounds(struct qz_output* output)
{
    wlr_box box;
    wlr_output_layout_get_box(output->server->output_layout, output->wlr_output, &box);
    return box;
}

void qz_output_frame(struct wl_listener* listener, void*)
{
    // This function is called every time an output is ready to display a frame, generally at the output's refresh rate (e.g. 60Hz)

    struct qz_output* output = wl_container_of(listener, output, frame);
    struct wlr_scene* scene = output->server->scene;

    struct wlr_scene_output* scene_output = wlr_scene_get_scene_output(scene, output->wlr_output);

    wlr_scene_output_commit(scene_output, nullptr);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

void qz_output_request_state(struct wl_listener* listener, void* data)
{
    // This function is called when the backend requests a new state for the output.
    // For example, wayland and X11 backends request a new mode when the output window is resized

    struct qz_output* output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state* event = static_cast<struct wlr_output_event_request_state*>(data);

    wlr_output_commit_state(output->wlr_output, event->state);
}

void qz_output_destroy(struct wl_listener* listener, void*)
{
    struct qz_output* output = wl_container_of(listener, output, destroy);

    QZ_UNLISTEN(output->frame);
    QZ_UNLISTEN(output->request_state);
    QZ_UNLISTEN(output->destroy);
    wl_list_remove(&output->link);

    wlr_scene_node_destroy(&output->background->node);

    delete output;
}

void qz_server_new_output(struct wl_listener* listener, void* data)
{
    // This event is raised by the backend when a new output (aka a display or monitor) becomes available

    struct qz_server* server = wl_container_of(listener, server, new_output);
    struct wlr_output* wlr_output = static_cast<struct wlr_output*>(data);

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
    if (mode != nullptr) {
        wlr_output_state_set_mode(&state, mode);
    }

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    struct qz_output* output = new qz_output{};
    output->wlr_output = wlr_output;
    output->server = server;

    wlr_output->data = output;

    QZ_LISTEN(wlr_output->events.frame,         output->frame,         qz_output_frame);
    QZ_LISTEN(wlr_output->events.request_state, output->request_state, qz_output_request_state);
    QZ_LISTEN(wlr_output->events.destroy,       output->destroy,       qz_output_destroy);

    wl_list_insert(&server->outputs, &output->link);

    // Add to output layout. `add_auto` arranges outputs from left-to-right in the order they appear
    // TODO: Configure size, position, scale
    struct wlr_output_layout_output* l_output = wlr_output_layout_add_auto(server->output_layout, wlr_output);
    struct wlr_scene_output* scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_output_layout, l_output, scene_output);
    output->scene_output = scene_output;

    // TODO: If this is set to 0,0,0,1 - results in incorrect damage when unfullscreening a window?
    float bg_color[] { 0.1f, 0.1f, 0.1f, 1.f };
    output->background = wlr_scene_rect_create(&server->scene->tree, wlr_output->width, wlr_output->height, bg_color);
}

void qz_server_output_layout_change(struct wl_listener* listener, void*)
{
    struct qz_server* server = wl_container_of(listener, server, output_layout_change);

    // TODO: Handled output removal, addition

    qz_output* output;
    wl_list_for_each(output, &server->outputs, link) {
        auto bounds = qz_output_get_bounds(output);
        if (output->background) {
            wlr_scene_node_set_position(&output->background->node, bounds.x, bounds.y);
            wlr_scene_rect_set_size(output->background, bounds.width, bounds.height);
        }
    }
}
