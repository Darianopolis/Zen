#include "quartz.hpp"

void qz_init(struct qz_server* server)
{
    server->wl_display = wl_display_create();
    server->backend = wlr_backend_autocreate(wl_display_get_event_loop(server->wl_display), nullptr);
    if (!server->backend) {
        wlr_log(WLR_ERROR, "failed to create wlr_backend");
        return;
    }

    server->modifier_key = WLR_MODIFIER_ALT;
    if (wlr_backend_is_drm(server->backend)) {
        wlr_log(WLR_INFO, "Created DRM backend");
        server->modifier_key = WLR_MODIFIER_LOGO;
    } else if (wlr_backend_is_wl(server->backend)) {
        wlr_log(WLR_INFO, "Created Wayland backend");
    } else if (wlr_backend_is_x11(server->backend)) {
        wlr_log(WLR_INFO, "Created X11 backend");
    } else if (wlr_backend_is_headless(server->backend)) {
        wlr_log(WLR_ERROR, "Falling back to headless backend");
    }

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) {
        wlr_log(WLR_ERROR, "failed to create wlr_renderer");
        return;
    }

    wlr_renderer_init_wl_display(server->renderer, server->wl_display);

    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (server->allocator == nullptr) {
        wlr_log(WLR_ERROR, "failed to create wlr_allocator");
        return;
    }

    // Create some hands-off wlroots interfaces
    server->compositor = wlr_compositor_create(server->wl_display, 5, server->renderer);
    server->subcompositor = wlr_subcompositor_create(server->wl_display);
    wlr_data_device_manager_create(server->wl_display);

    wlr_export_dmabuf_manager_v1_create(server->wl_display);
    wlr_screencopy_manager_v1_create(server->wl_display);
    wlr_data_control_manager_v1_create(server->wl_display);
    wlr_primary_selection_v1_device_manager_create(server->wl_display);
    wlr_viewporter_create(server->wl_display);
    wlr_single_pixel_buffer_manager_v1_create(server->wl_display);
    wlr_fractional_scale_manager_v1_create(server->wl_display, 1);
    wlr_presentation_create(server->wl_display, server->backend, 2);
    wlr_alpha_modifier_v1_create(server->wl_display);
    wlr_linux_drm_syncobj_manager_v1_create(server->wl_display, 1, wlr_backend_get_drm_fd(server->backend));

    server->output_layout = wlr_output_layout_create(server->wl_display);

    wl_list_init(&server->outputs);
    QZ_LISTEN(server->backend->events.new_output, server->new_output, qz_server_new_output);

    server->scene = wlr_scene_create();
    server->scene_output_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);

    wl_list_init(&server->toplevels);
    server->xdg_shell = wlr_xdg_shell_create(server->wl_display, 3);
    QZ_LISTEN(server->xdg_shell->events.new_toplevel, server->new_xdg_toplevel, qz_server_new_xdg_toplevel);
    QZ_LISTEN(server->xdg_shell->events.new_popup,    server->new_xdg_popup,    qz_server_new_xdg_popup);

    server->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

    server->cursor_manager = wlr_xcursor_manager_create(nullptr, 24);

    server->cursor_mode = QZ_CURSOR_PASSTHROUGH;
    QZ_LISTEN(server->cursor->events.motion,          server->cursor_motion,          qz_server_cursor_motion);
    QZ_LISTEN(server->cursor->events.motion_absolute, server->cursor_motion_absolute, qz_server_cursor_motion_absolute);
    QZ_LISTEN(server->cursor->events.button,          server->cursor_button,          qz_server_cursor_button);
    QZ_LISTEN(server->cursor->events.axis,            server->cursor_axis,            qz_server_cursor_axis);
    QZ_LISTEN(server->cursor->events.frame,           server->cursor_frame,           qz_server_cursor_frame);

    wl_list_init(&server->keyboards);
    QZ_LISTEN(server->backend->events.new_input, server->new_input, qz_server_new_input);

    server->seat = wlr_seat_create(server->wl_display, "seat0");
    QZ_LISTEN(              server->seat->events.request_set_cursor,    server->request_cursor,        qz_seat_request_set_cursor);
    QZ_LISTEN(server->seat->pointer_state.events.focus_change,          server->pointer_focus_change,  qz_seat_pointer_focus_change);
    QZ_LISTEN(              server->seat->events.request_set_selection, server->request_set_selection, qz_seat_request_set_selection);

    // Make sure containing X server does not leak through
    unsetenv("DISPLAY");

#ifdef QZ_XWAYLAND
    qz_init_xwayland(server);
#endif
}

void qz_run(struct qz_server* server, char* startup_cmd)
{
    const char* socket = wl_display_add_socket_auto(server->wl_display);
    if (!socket) {
        wlr_backend_destroy(server->backend);
        return;
    }

    if (!wlr_backend_start(server->backend)) {
        wlr_backend_destroy(server->backend);
        wl_display_destroy(server->wl_display);
        return;
    }

    setenv("WAYLAND_DISPLAY", socket, true);

    // TODO: Set this for applications that we've checked work in Wayland mode inside of Quartz
    setenv("ELECTRON_OZONE_PLATFORM_HINT", "auto", true);
    setenv("SDL_VIDEO_DRIVER", "wayland", true);

    if (startup_cmd) {
        qz_spawn("/bin/sh", (const char* const[]){"/bin/sh", "-c", startup_cmd, nullptr});
    }

    wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
    wl_display_run(server->wl_display);
}

void qz_cleanup(struct qz_server* server)
{
    wl_display_destroy_clients(server->wl_display);
    // TODO: Wait for clients to die properly

#ifdef QZ_XWAYLAND
    qz_destroy_xwayland(server);
#endif

    QZ_UNLISTEN(server->new_xdg_toplevel);
    QZ_UNLISTEN(server->new_xdg_popup);
    QZ_UNLISTEN(server->cursor_motion);
    QZ_UNLISTEN(server->cursor_motion_absolute);
    QZ_UNLISTEN(server->cursor_button);
    QZ_UNLISTEN(server->cursor_axis);
    QZ_UNLISTEN(server->cursor_frame);

    QZ_UNLISTEN(server->new_input);
    QZ_UNLISTEN(server->request_cursor);
    QZ_UNLISTEN(server->pointer_focus_change);
    QZ_UNLISTEN(server->request_set_selection);

    QZ_UNLISTEN(server->new_output);

    wlr_scene_node_destroy(&server->scene->tree.node);
    wlr_xcursor_manager_destroy(server->cursor_manager);
    wlr_cursor_destroy(server->cursor);
    wlr_allocator_destroy(server->allocator);
    wlr_renderer_destroy(server->renderer);
    wlr_backend_destroy(server->backend);
    wl_display_destroy(server->wl_display);
}

int qz_main(int argc, char* argv[])
{
    wlr_log_init(WLR_INFO, nullptr);
    char* startup_cmd = nullptr;

    int c;
    while ((c = getopt(argc, argv, "s:h")) != -1) {
        switch (c) {
            case 's':
                startup_cmd = optarg;
                break;
            default:
                printf("Usage: %s [-s startup command]\n", argv[0]);
                return 0;
        }
    }

    if (optind < argc) {
        printf("Usage: %s [-s startup command]\n", argv[0]);
        return 0;
    }

    // Init

    struct qz_server server = {};
    qz_init(&server);

    // Run

    qz_run(&server, startup_cmd);

    // Closing

    qz_cleanup(&server);

    return 0;
}

int main(int argc, char* argv[])
{
    return qz_main(argc, argv);
}