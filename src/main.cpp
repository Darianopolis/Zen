#include "pch.hpp"
#include "core.hpp"
#include "log.hpp"

using namespace std::literals;

struct startup_options
{
    const char* xwayland_socket;
    const char* startup_cmd;
    const char* log_file;
    uint32_t additional_outputs;
    bool ctrl_mod;
};

static
void init(Server* server, const startup_options& options)
{
    server->display = wl_display_create();
    server->backend = wlr_backend_autocreate(wl_display_get_event_loop(server->display), nullptr);
    if (!server->backend) {
        log_error("Failed to create wlr_backend");
        return;
    }

    server->main_modifier = WLR_MODIFIER_LOGO;
    server->main_modifier_keysym_left = XKB_KEY_Super_L;
    server->main_modifier_keysym_right = XKB_KEY_Super_R;

    bool nested = false;

    auto for_each_backend = [&](wlr_backend* backend) {
        if (!wlr_backend_is_wl(backend) && !wlr_backend_is_x11(backend)) return;

        nested = true;

        if (options.ctrl_mod) {
            server->main_modifier = WLR_MODIFIER_CTRL;
            server->main_modifier_keysym_left = XKB_KEY_Control_L;
            server->main_modifier_keysym_right = XKB_KEY_Control_R;
        } else {
            server->main_modifier = WLR_MODIFIER_ALT;
            server->main_modifier_keysym_left = XKB_KEY_Alt_L;
            server->main_modifier_keysym_right = XKB_KEY_Alt_R;
        }

        log_warn("Running compositor nested, mouse constraints will be silently ignored!");
        server->debug.ignore_mouse_constraints = true;

        for (uint32_t i = 0; i < options.additional_outputs; ++i) {
            wlr_wl_output_create(backend);
        }
    };
    wlr_multi_for_each_backend(server->backend, [](wlr_backend* b, void* d) { (*static_cast<decltype(for_each_backend)*>(d))(b); }, &for_each_backend);

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) {
        log_error("Failed to create wlr_renderer");
        return;
    }

    wlr_renderer_init_wl_display(server->renderer, server->display);

    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (server->allocator == nullptr) {
        log_error("Failed to create wlr_allocator");
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

    wlr_xdg_output_manager_v1_create(server->display, server->output_layout);

    server->scene = wlr_scene_create();
    for (Strata strata : server->layers.enum_values) {
        server->layers[strata] = wlr_scene_tree_create(&server->scene->tree);
    }

    server->scene_output_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);
    server->drag_icon_parent = wlr_scene_tree_create(server->layers[Strata::overlay]);

    server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
    server->listeners.listen(&server->xdg_shell->events.new_toplevel, server, server_new_toplevel);
    server->listeners.listen(&server->xdg_shell->events.new_popup,    server, server_new_popup);

    server->layer_shell = wlr_layer_shell_v1_create(server->display, 3);
    server->listeners.listen(&server->layer_shell->events.new_surface, server, server_new_layer_surface);

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

    server->cursor_manager = wlr_xcursor_manager_create(nullptr, cursor_size);
	setenv("XCURSOR_SIZE", std::to_string(cursor_size).c_str(), 1);

    if (nested) {
        server->pointer.debug_visual_half_extent = 4;
        server->pointer.debug_visual = wlr_scene_rect_create(server->layers[Strata::debug], server->pointer.debug_visual_half_extent * 2, server->pointer.debug_visual_half_extent * 2, Color{}.values);
        pointer_update_debug_visual(server);
    }

    server->interaction_mode = InteractionMode::passthrough;
    server->listeners.listen(&server->cursor->events.motion,          server, server_cursor_motion);
    server->listeners.listen(&server->cursor->events.motion_absolute, server, server_cursor_motion_absolute);
    server->listeners.listen(&server->cursor->events.button,          server, server_cursor_button);
    server->listeners.listen(&server->cursor->events.axis,            server, server_cursor_axis);
    server->listeners.listen(&server->cursor->events.frame,           server, server_cursor_frame);

    server->listeners.listen(&server->backend->events.new_input, server, server_new_input);

    server->seat = wlr_seat_create(server->display, "seat0");
    server->listeners.listen(&              server->seat->events.request_set_cursor,    server, seat_request_set_cursor);
    server->listeners.listen(&server->seat->keyboard_state.events.focus_change,         server, seat_keyboard_focus_change);
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

    server->debug.original_cwd = std::filesystem::current_path();
    chdir(getenv("HOME"));
    setenv("WAYLAND_DISPLAY", socket, true);
    setenv("XDG_CURRENT_DESKTOP", PROGRAM_NAME, true);

    if (options.xwayland_socket) {
        setenv("DISPLAY", options.xwayland_socket, true);
        spawn("xwayland-satellite", {"xwayland-satellite", options.xwayland_socket});
    } else {
        unsetenv("DISPLAY");
    }

    if (options.startup_cmd) {
        spawn("/bin/sh", {"/bin/sh", "-c", options.startup_cmd});
    }

    log_info("Running Wayland compositor on WAYLAND_DISPLAY={}", socket);
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
  --outputs  [count]    number of outputs to spawn (in nested mode)
)";

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
        } else if ("--ctrl-mod"sv == arg) {
            options.ctrl_mod = true;
        } else if ("--outputs"sv == arg) {
            const char* p = param();
            int v = 1;
            std::from_chars_result res = std::from_chars(p, p + strlen(p), v);
            if (!res) print_usage();
            options.additional_outputs = std::max(1, v) - 1;
        } else if ("--help"sv == arg) {
            print_usage();
        }
    }

    init_log(LogLevel::trace, WLR_SILENT, options.log_file);

    // Init

    Server server = {};
    init(&server, options);

    // Run

    run(&server, options);

    // Closing

    cleanup(&server);

    return 0;
}
