#include "quartz.h"

void qz_output_frame(struct wl_listener* listener, void*)
{
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
    struct qz_output* output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state* event = data;

    wlr_output_commit_state(output->wlr_output, event->state);
}

void qz_output_destroy(struct wl_listener* listener, void*)
{
    struct qz_output* output = wl_container_of(listener, output, destroy);

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);

    free(output);
}

void qz_server_new_output(struct wl_listener* listener, void* data)
{
    struct qz_server* server = wl_container_of(listener, server, new_output);
    struct wlr_output* wlr_output = data;

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

    struct qz_output* output = calloc(1, sizeof(*output));
    output->wlr_output = wlr_output;
    output->server = server;

    QZ_LISTEN(wlr_output->events.frame,         output->frame,         qz_output_frame);
    QZ_LISTEN(wlr_output->events.request_state, output->request_state, qz_output_request_state);
    QZ_LISTEN(wlr_output->events.destroy,       output->destroy,       qz_output_destroy);

    wl_list_insert(&server->outputs, &output->link);

    // Add to output layout. `add_auto` arranges outputs from left-to-right in the order they appear
    // TODO: Configure size, position, scale
    struct wlr_output_layout_output* l_output = wlr_output_layout_add_auto(server->output_layout, wlr_output);
    struct wlr_scene_output* scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_output_layout, l_output, scene_output);
}
