#include "core.hpp"

#include <string_view>
#include <iostream>

using namespace std::literals;

struct startup_options
{
    const char* xwayland_socket;
    const char* startup_cmd;
    const char* log_file;
};

static
void init(Server* server, const startup_options& /* options */)
{
    server->display = wl_display_create();
    server->backend = wlr_backend_autocreate(wl_display_get_event_loop(server->display), nullptr);
    if (!server->backend) {
        wlr_log(WLR_ERROR, "Failed to create wlr_backend");
        return;
    }

    server->modifier_key = WLR_MODIFIER_LOGO;
    wlr_multi_for_each_backend(server->backend, [](wlr_backend* backend, void* data) {
        if (wlr_backend_is_wl(backend) || wlr_backend_is_x11(backend)) {
            static_cast<Server*>(data)->modifier_key = WLR_MODIFIER_ALT;
            for (uint32_t i = 0; i < additional_outputs; ++i) {
                wlr_wl_output_create(backend);
            }
        }
    }, server);

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) {
        wlr_log(WLR_ERROR, "Failed to create wlr_renderer");
        return;
    }

    wlr_renderer_init_wl_display(server->renderer, server->display);

    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (server->allocator == nullptr) {
        wlr_log(WLR_ERROR, "Failed to create wlr_allocator");
        return;
    }

    // TODO: Split initialization of subsystems

    // Create some hands-off wlroots interfaces
    server->compositor = wlr_compositor_create(server->display, 5, server->renderer);
    server->subcompositor = wlr_subcompositor_create(server->display);
    wlr_data_device_manager_create(server->display);
    wlr_export_dmabuf_manager_v1_create(server->display);
    wlr_screencopy_manager_v1_create(server->display);
    wlr_data_control_manager_v1_create(server->display);
    wlr_primary_selection_v1_device_manager_create(server->display);
    wlr_viewporter_create(server->display);
    wlr_single_pixel_buffer_manager_v1_create(server->display);
    wlr_fractional_scale_manager_v1_create(server->display, 1);
    wlr_presentation_create(server->display, server->backend, 2);
    wlr_alpha_modifier_v1_create(server->display);

    // NOTE: syncobj is not currently usuable due to driver/toolkit issues
    //
    //       wlr_linux_drm_syncobj_manager_v1_create(server->wl_display, 1, wlr_backend_get_drm_fd(server->backend));

    server->output_layout = wlr_output_layout_create(server->display);
    server->listeners.listen(&server->output_layout->events.change, server, server_output_layout_change);

    server->listeners.listen(&server->backend->events.new_output, server, server_new_output);

    server->scene = wlr_scene_create();
    server->scene_output_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);
    // TODO: drag icon should be in a layer above everything else (implement layer shell extension!)
    server->drag_icon_parent = wlr_scene_tree_create(&server->scene->tree);

    server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
    server->listeners.listen(&server->xdg_shell->events.new_toplevel, server, server_new_toplevel);
    server->listeners.listen(&server->xdg_shell->events.new_popup,    server, server_new_popup);

    // Decorations:
    //  We enable enough decoration functionality to tell clients *not* to render their
    //  own decorations but we'll render our border regardless of whether clients comply.
    wlr_server_decoration_manager_set_default_mode(
        wlr_server_decoration_manager_create(server->display),
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
    server->xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(server->display);
    server->listeners.listen(&server->xdg_decoration_manager->events.new_toplevel_decoration, server, decoration_new);

    server->pointer.pointer_constraints = wlr_pointer_constraints_v1_create(server->display);
    server->listeners.listen(&server->pointer.pointer_constraints->events.new_constraint, server, server_pointer_constraint_new);

    server->pointer.relative_pointer_manager = wlr_relative_pointer_manager_v1_create(server->display);

    server->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

    server->cursor_manager = wlr_xcursor_manager_create(nullptr, 24);

    server->pointer.debug_visual = wlr_scene_rect_create(&server->scene->tree, 12, 12, Color{1, 0, 0, 1}.values);
    wlr_scene_node_set_enabled(&server->pointer.debug_visual->node, false);

    server->cursor_mode = CursorMode::passthrough;
    server->listeners.listen(&server->cursor->events.motion,          server, server_cursor_motion);
    server->listeners.listen(&server->cursor->events.motion_absolute, server, server_cursor_motion_absolute);
    server->listeners.listen(&server->cursor->events.button,          server, server_cursor_button);
    server->listeners.listen(&server->cursor->events.axis,            server, server_cursor_axis);
    server->listeners.listen(&server->cursor->events.frame,           server, server_cursor_frame);

    server->listeners.listen(&server->backend->events.new_input, server, server_new_input);

    server->seat = wlr_seat_create(server->display, "seat0");
    server->listeners.listen(&              server->seat->events.request_set_cursor,    server, seat_request_set_cursor);
    server->listeners.listen(&server->seat->pointer_state.events.focus_change,          server, seat_pointer_focus_change);
    server->listeners.listen(&              server->seat->events.request_set_selection, server, seat_request_set_selection);
    server->listeners.listen(&              server->seat->events.request_start_drag,    server, seat_request_start_drag);
    server->listeners.listen(&              server->seat->events.start_drag,            server, seat_start_drag);

    zone_init(server);
}

void run(Server* server, const startup_options& options)
{
    const char* socket = wl_display_add_socket_auto(server->display);
    if (!socket) {
        wlr_backend_destroy(server->backend);
        return;
    }

    if (!wlr_backend_start(server->backend)) {
        wlr_backend_destroy(server->backend);
        wl_display_destroy(server->display);
        return;
    }

    setenv("WAYLAND_DISPLAY", socket, true);

    if (options.xwayland_socket) {
        setenv("DISPLAY", options.xwayland_socket, true);
        spawn("xwayland-satellite", {"xwayland-satellite", options.xwayland_socket});
    } else {
        unsetenv("DISPLAY");
    }

    if (options.startup_cmd) {
        spawn("/bin/sh", {"/bin/sh", "-c", options.startup_cmd});
    }

    wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
    wl_display_run(server->display);
}

void cleanup(Server* server)
{
    wl_display_destroy_clients(server->display);
    // TODO: Wait for clients to die properly

    server->listeners.clear();

    wlr_xcursor_manager_destroy(server->cursor_manager);
    wlr_cursor_destroy(server->cursor);
    wlr_allocator_destroy(server->allocator);
    wlr_renderer_destroy(server->renderer);
    wlr_backend_destroy(server->backend);
    wl_display_destroy(server->display);
    wlr_scene_node_destroy(&server->scene->tree.node);
}

constexpr const char* help_prompt = R"(Usage: %s [options]
  --xwayland [socket]   specify X11 socket
  --log-file [path]     log to file
  --startup  [cmd]      startup command
)";

static struct {
    FILE* log_file;
} log_state = {};

void log_callback(wlr_log_importance importance, const char *fmt, va_list args)
{
    if (importance > WLR_INFO) return;

    char buffer[65'536];
    int len = vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);

    buffer[len++] = '\n';
    buffer[len] = '\0';

    std::cout << buffer;
    if (log_state.log_file) {
        fwrite(buffer, 1, len, log_state.log_file);
        fflush(log_state.log_file);
    }
}

int main(int argc, char* argv[])
{
    startup_options options = {};

    auto print_usage = [&] {
        printf(help_prompt, PROGRAM_NAME);
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
        log_state.log_file = fopen(options.log_file, "wb");
    }
    wlr_log_init(WLR_INFO, log_callback);

    // Init

    Server server = {};
    init(&server, options);

    // Run

    run(&server, options);

    // Closing

    cleanup(&server);

    return 0;
}
