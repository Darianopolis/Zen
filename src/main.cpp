#include "core.hpp"

#include <string_view>
#include <iostream>

using namespace std::literals;

struct qz_startup_options
{
    const char* xwayland_socket;
    const char* startup_cmd;
    const char* log_file;
};

static
void qz_init(qz_server* server, const qz_startup_options& /* options */)
{
    server->wl_display = wl_display_create();
    server->backend = wlr_backend_autocreate(wl_display_get_event_loop(server->wl_display), nullptr);
    if (!server->backend) {
        wlr_log(WLR_ERROR, "Failed to create wlr_backend");
        return;
    }

    server->modifier_key = WLR_MODIFIER_LOGO;
    wlr_multi_for_each_backend(server->backend, [](wlr_backend* backend, void* data) {
        if (wlr_backend_is_wl(backend) || wlr_backend_is_x11(backend)) {
            static_cast<qz_server*>(data)->modifier_key = WLR_MODIFIER_ALT;
        }
    }, server);

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) {
        wlr_log(WLR_ERROR, "Failed to create wlr_renderer");
        return;
    }

    wlr_renderer_init_wl_display(server->renderer, server->wl_display);

    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (server->allocator == nullptr) {
        wlr_log(WLR_ERROR, "Failed to create wlr_allocator");
        return;
    }

    // TODO: Split initialization of subsystems

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

    // NOTE: syncobj is not currently usuable due to driver/toolkit issues
    //
    //       wlr_linux_drm_syncobj_manager_v1_create(server->wl_display, 1, wlr_backend_get_drm_fd(server->backend));

    server->output_layout = wlr_output_layout_create(server->wl_display);
    server->listeners.listen(&server->output_layout->events.change, server, qz_server_output_layout_change);

    server->listeners.listen(&server->backend->events.new_output, server, qz_server_new_output);

    server->scene = wlr_scene_create();
    server->scene_output_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);
    // TODO: drag icon should be in a layer above everything else (implement layer shell extension!)
    server->drag_icon_parent = wlr_scene_tree_create(&server->scene->tree);

    server->xdg_shell = wlr_xdg_shell_create(server->wl_display, 3);
    server->listeners.listen(&server->xdg_shell->events.new_toplevel, server, qz_server_new_toplevel);
    server->listeners.listen(&server->xdg_shell->events.new_popup,    server, qz_server_new_popup);

    // Decorations:
    //  We enable enough decoration functionality to tell clients *not* to render their
    //  own decorations but we'll render our border regardless of whether clients comply.
    wlr_server_decoration_manager_set_default_mode(
        wlr_server_decoration_manager_create(server->wl_display),
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
    server->xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(server->wl_display);
    server->listeners.listen(&server->xdg_decoration_manager->events.new_toplevel_decoration, server, qz_decoration_new);

    server->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

    server->cursor_manager = wlr_xcursor_manager_create(nullptr, 24);

    server->cursor_mode = qz_cursor_mode::passthrough;
    server->listeners.listen(&server->cursor->events.motion,          server, qz_server_cursor_motion);
    server->listeners.listen(&server->cursor->events.motion_absolute, server, qz_server_cursor_motion_absolute);
    server->listeners.listen(&server->cursor->events.button,          server, qz_server_cursor_button);
    server->listeners.listen(&server->cursor->events.axis,            server, qz_server_cursor_axis);
    server->listeners.listen(&server->cursor->events.frame,           server, qz_server_cursor_frame);

    server->listeners.listen(&server->backend->events.new_input, server, qz_server_new_input);

    server->seat = wlr_seat_create(server->wl_display, "seat0");
    server->listeners.listen(&              server->seat->events.request_set_cursor,    server, qz_seat_request_set_cursor);
    server->listeners.listen(&server->seat->pointer_state.events.focus_change,          server, qz_seat_pointer_focus_change);
    server->listeners.listen(&              server->seat->events.request_set_selection, server, qz_seat_request_set_selection);
    server->listeners.listen(&              server->seat->events.request_start_drag,    server, qz_seat_request_start_drag);
    server->listeners.listen(&              server->seat->events.start_drag,            server, qz_seat_start_drag);

    qz_zone_init(server);
}

void qz_run(qz_server* server, const qz_startup_options& options)
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

    // TODO: Set this per spawned application
    setenv("ELECTRON_OZONE_PLATFORM_HINT", "auto", true);
    setenv("SDL_VIDEO_DRIVER", "wayland", true);

    if (options.xwayland_socket) {
        setenv("DISPLAY", options.xwayland_socket, true);
    } else {
        unsetenv("DISPLAY");
    }

    if (options.startup_cmd) {
        qz_spawn("/bin/sh", {"/bin/sh", "-c", options.startup_cmd});
    }

    wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
    wl_display_run(server->wl_display);
}

void qz_cleanup(qz_server* server)
{
    wl_display_destroy_clients(server->wl_display);
    // TODO: Wait for clients to die properly

    server->listeners.clear();

    wlr_xcursor_manager_destroy(server->cursor_manager);
    wlr_cursor_destroy(server->cursor);
    wlr_allocator_destroy(server->allocator);
    wlr_renderer_destroy(server->renderer);
    wlr_backend_destroy(server->backend);
    wl_display_destroy(server->wl_display);
    wlr_scene_node_destroy(&server->scene->tree.node);
}

constexpr const char* qz_help_prompt = R"(Usage: %s [options]
  --xwayland [socket]   specify X11 socket
  --log-file [path]     log to file
  --startup  [cmd]      startup command
)";

static struct {
    FILE* log_file;
} qz_log_state = {};

void qz_log_callback(wlr_log_importance importance, const char *fmt, va_list args)
{
    if (importance > WLR_INFO) return;

    char buffer[65'536];
    int len = vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);

    buffer[len++] = '\n';
    buffer[len] = '\0';

    std::cout << buffer;
    if (qz_log_state.log_file) {
        fwrite(buffer, 1, len, qz_log_state.log_file);
        fflush(qz_log_state.log_file);
    }
}

int qz_main(int argc, char* argv[])
{
    qz_startup_options options = {};

    auto print_usage = [&] {
        printf(qz_help_prompt, QZ_PROGRAM_NAME);
        exit(0);
    };

    for (int i = 1; i < argc; ++i) {
        const char* arg = {argv[i]};
        auto param = [&] {
            if (++i >= argc) print_usage();
            return argv[i];
        };

        if        ("--log-file"sv == arg) {
            options.log_file = param();
        } else if ("--startup"sv == arg) {
            options.startup_cmd = param();
        } else if ("--xwayland"sv == arg) {
            options.xwayland_socket = param();
        } else if ("--help"sv == arg) {
            print_usage();
        }
    }

    if (options.log_file) {
        qz_log_state.log_file = fopen(options.log_file, "wb");
    }
    wlr_log_init(WLR_INFO, qz_log_callback);

    // Init

    qz_server server = {};
    qz_init(&server, options);

    // Run

    qz_run(&server, options);

    // Closing

    qz_cleanup(&server);

    return 0;
}

int main(int argc, char* argv[])
{
    return qz_main(argc, argv);
}
