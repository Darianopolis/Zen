#include "quartz.h"

#include <wlr/render/vulkan.h>

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

    struct qz_server server = {};
    server.wl_display = wl_display_create();
    server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), nullptr);
    if (!server.backend) {
        wlr_log(WLR_ERROR, "failed to create wlr_backend");
        return 1;
    }

    server.renderer = wlr_renderer_autocreate(server.backend);
    if (!server.renderer) {
        wlr_log(WLR_ERROR, "failed to create wlr_renderer");
        return 1;
    }

    wlr_renderer_init_wl_display(server.renderer, server.wl_display);

    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if (server.allocator == nullptr) {
        wlr_log(WLR_ERROR, "failed to create wlr_allocator");
        return 1;
    }

    // Create some hands-off wlroots interfaces
    wlr_compositor_create(server.wl_display, 5, server.renderer);
    wlr_subcompositor_create(server.wl_display);
    wlr_data_device_manager_create(server.wl_display);

    server.output_layout = wlr_output_layout_create(server.wl_display);

    wl_list_init(&server.outputs);
    QZ_LISTEN(server.backend->events.new_output, server.new_output, qz_server_new_output);

    server.scene = wlr_scene_create();
    server.scene_output_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

    wl_list_init(&server.toplevels);
    server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
    QZ_LISTEN(server.xdg_shell->events.new_toplevel, server.new_xdg_toplevel, qz_server_new_xdg_toplevel);
    QZ_LISTEN(server.xdg_shell->events.new_popup, server.new_xdg_popup, qz_server_new_xdg_popup);

    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

    server.cursor_manager = wlr_xcursor_manager_create(nullptr, 24);

    server.cursor_mode = QZ_CURSOR_PASSTHROUGH;
    QZ_LISTEN(server.cursor->events.motion,          server.cursor_motion,          qz_server_cursor_motion);
    QZ_LISTEN(server.cursor->events.motion_absolute, server.cursor_motion_absolute, qz_server_cursor_motion_absolute);
    QZ_LISTEN(server.cursor->events.button,          server.cursor_button,          qz_server_cursor_button);
    QZ_LISTEN(server.cursor->events.axis,            server.cursor_axis,            qz_server_cursor_axis);
    QZ_LISTEN(server.cursor->events.frame,           server.cursor_frame,           qz_server_cursor_frame);

    wl_list_init(&server.keyboards);
    QZ_LISTEN(server.backend->events.new_input, server.new_input, qz_server_new_input);

    server.seat = wlr_seat_create(server.wl_display, "seat0");
    QZ_LISTEN(              server.seat->events.request_set_cursor,    server.request_cursor,        qz_seat_request_set_cursor);
    QZ_LISTEN(server.seat->pointer_state.events.focus_change,          server.pointer_focus_change,  qz_seat_pointer_focus_change);
    QZ_LISTEN(              server.seat->events.request_set_selection, server.request_set_selection, qz_seat_request_set_selection);

    const char* socket = wl_display_add_socket_auto(server.wl_display);
    if (!socket) {
        wlr_backend_destroy(server.backend);
        return 1;
    }

    if (!wlr_backend_start(server.backend)) {
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    if (startup_cmd) {
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c", startup_cmd, nullptr);
        }
    }

    wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
    wl_display_run(server.wl_display);

    // Closing

    wl_display_destroy_clients(server.wl_display);

    wl_list_remove(&server.new_xdg_toplevel.link);
    wl_list_remove(&server.new_xdg_popup.link);
    wl_list_remove(&server.cursor_motion.link);
    wl_list_remove(&server.cursor_motion_absolute.link);
    wl_list_remove(&server.cursor_button.link);
    wl_list_remove(&server.cursor_axis.link);
    wl_list_remove(&server.cursor_frame.link);

    wl_list_remove(&server.new_input.link);
    wl_list_remove(&server.request_cursor.link);
    wl_list_remove(&server.pointer_focus_change.link);
    wl_list_remove(&server.request_set_selection.link);

    wl_list_remove(&server.new_output.link);

    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_xcursor_manager_destroy(server.cursor_manager);
    wlr_cursor_destroy(server.cursor);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.wl_display);

    return 0;
}

int main(int argc, char* argv[])
{
    return qz_main(argc, argv);
}