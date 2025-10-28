#include "pch.hpp"
#include "core.hpp"
#include "log.hpp"

using namespace std::literals;

struct startup_options
{
    std::string xwayland_socket;
    std::string log_file;
    std::span<const std::string_view> startup_command;
    std::vector<std::string_view> startup_shell_commands;
    uint32_t additional_outputs;
    bool ctrl_mod;
};

#define USE_SYNCOBJ 0
#define USE_VULKAN 1

static
void init(Server* server, const startup_options& options)
{
    server->debug.original_cwd = std::filesystem::current_path();
    chdir(getenv("HOME"));

    // Core

    server->display = wl_display_create();
    server->backend = wlr_backend_autocreate(wl_display_get_event_loop(server->display), &server->session);
    if (!server->backend) {
        log_error("Failed to create wlr_backend");
        return;
    }

    // Handler modifiers and nested detection

    server->main_modifier = WLR_MODIFIER_LOGO;
    server->main_modifier_keysym_left = XKB_KEY_Super_L;
    server->main_modifier_keysym_right = XKB_KEY_Super_R;

    auto for_each_backend = [&](wlr_backend* backend) {
        if (!wlr_backend_is_wl(backend) && !wlr_backend_is_x11(backend)) return;

        server->debug.is_nested = true;

        if (options.ctrl_mod) {
            server->main_modifier = WLR_MODIFIER_CTRL;
            server->main_modifier_keysym_left = XKB_KEY_Control_L;
            server->main_modifier_keysym_right = XKB_KEY_Control_R;
        } else {
            server->main_modifier = WLR_MODIFIER_ALT;
            server->main_modifier_keysym_left = XKB_KEY_Alt_L;
            server->main_modifier_keysym_right = XKB_KEY_Alt_R;
        }

        for (uint32_t i = 0; i < options.additional_outputs; ++i) {
            wlr_wl_output_create(backend);
        }
    };
    wlr_multi_for_each_backend(server->backend, [](wlr_backend* b, void* d) { (*static_cast<decltype(for_each_backend)*>(d))(b); }, &for_each_backend);

    // Renderer

#if USE_VULKAN
    server->renderer = wlr_vk_renderer_create_with_drm_fd(wlr_backend_get_drm_fd(server->backend));
#else
    server->renderer = wlr_renderer_autocreate(server->backend);
#endif
    wlr_renderer_init_wl_display(server->renderer, server->display);
    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);

    // Hands-off wlroots interfaces

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

#if USE_SYNCOBJ
    wlr_tearing_control_manager_v1_create(server->display, 1);
    wlr_linux_drm_syncobj_manager_v1_create(server->display, 1, wlr_backend_get_drm_fd(server->backend));
#endif

    // XDG Activation

    server->activation = wlr_xdg_activation_v1_create(server->display);
    server->listeners.listen(&server->activation->events.request_activate, server, request_activate);

    // XDG Foreign

    server->foreign_registry = wlr_xdg_foreign_registry_create(server->display);
    wlr_xdg_foreign_v2_create(server->display, server->foreign_registry);

    server->foreign_toplevel_manager =  wlr_foreign_toplevel_manager_v1_create(server->display);

    // Outputs

    server->output_layout = wlr_output_layout_create(server->display);
    server->listeners.listen(&server->output_layout->events.change,     server, output_layout_change);
    server->listeners.listen(&      server->backend->events.new_output, server, output_new);

    wlr_xdg_output_manager_v1_create(server->display, server->output_layout);

    // Scene

    server->scene = wlr_scene_create();
    for (Strata strata : server->layers.enum_values) {
        server->layers[strata] = wlr_scene_tree_create(&server->scene->tree);
    }

    server->scene_output_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);
    server->drag_icon_parent = wlr_scene_tree_create(server->layers[Strata::overlay]);

    // XDG Shell

    server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
    server->listeners.listen(&server->xdg_shell->events.new_toplevel, server, toplevel_new);
    server->listeners.listen(&server->xdg_shell->events.new_popup,    server, popup_new);

    // Layer Shell

    server->layer_shell = wlr_layer_shell_v1_create(server->display, 3);
    server->listeners.listen(&server->layer_shell->events.new_surface, server, layer_surface_new);

    // Decorations

    wlr_server_decoration_manager_set_default_mode(
        wlr_server_decoration_manager_create(server->display),
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
    server->xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(server->display);
    server->listeners.listen(&server->xdg_decoration_manager->events.new_toplevel_decoration, server, decoration_new);

    // Seat + Input

    server->seat = wlr_seat_create(server->display, "seat0");
    server->listeners.listen(&               server->seat->events.request_set_cursor,    server, seat_request_set_cursor);
    server->listeners.listen(&server->seat->keyboard_state.events.focus_change,          server, seat_keyboard_focus_change);
    server->listeners.listen(& server->seat->pointer_state.events.focus_change,          server, seat_pointer_focus_change);
    server->listeners.listen(&               server->seat->events.request_set_selection, server, seat_request_set_selection);
    server->listeners.listen(&               server->seat->events.request_start_drag,    server, seat_request_start_drag);
    server->listeners.listen(&               server->seat->events.start_drag,            server, seat_start_drag);

    server->listeners.listen(&server->backend->events.new_input, server, input_new);

    // Pointer + Cursor

    server->pointer.pointer_constraints = wlr_pointer_constraints_v1_create(server->display);
    server->listeners.listen(&server->pointer.pointer_constraints->events.new_constraint, server, pointer_constraint_new);

    server->pointer.relative_pointer_manager = wlr_relative_pointer_manager_v1_create(server->display);

    server->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

    server->cursor_manager = wlr_xcursor_manager_create(nullptr, cursor_size);
	env_set(server, "XCURSOR_SIZE", std::to_string(cursor_size));

    server->interaction_mode = InteractionMode::passthrough;
    server->listeners.listen(&server->cursor->events.motion,          server, cursor_motion);
    server->listeners.listen(&server->cursor->events.motion_absolute, server, cursor_motion_absolute);
    server->listeners.listen(&server->cursor->events.button,          server, cursor_button);
    server->listeners.listen(&server->cursor->events.axis,            server, cursor_axis);
    server->listeners.listen(&server->cursor->events.frame,           server, cursor_frame);

    server->pointer.debug_visual_half_extent = 4;
    server->pointer.debug_visual = wlr_scene_rect_create(server->layers[Strata::debug], server->pointer.debug_visual_half_extent * 2, server->pointer.debug_visual_half_extent * 2, Color{}.values);
    wlr_scene_node_set_enabled(&server->pointer.debug_visual->node, false);

    update_cursor_state(server);

    // Zone window management

    zone_init(server);
}

void run(Server* server, const startup_options& options)
{
    const char* socket = wl_display_add_socket_auto(server->display);
    wlr_backend_start(server->backend);

    env_set(server, "WAYLAND_DISPLAY", socket);
    env_set(server, "XDG_CURRENT_DESKTOP", PROGRAM_NAME);

    if (!options.xwayland_socket.empty()) {
        env_set(server, "DISPLAY", options.xwayland_socket);
        spawn(server, "xwayland-satellite", {"xwayland-satellite", options.xwayland_socket});
    } else {
        env_set(server, "DISPLAY", std::nullopt);
    }

    // Config server

    ipc_server_init(server);

    // Startup command

    for (std::string_view shell_cmd : options.startup_shell_commands) {
        spawn(server, "sh", {"sh", shell_cmd}, {}, server->debug.original_cwd.c_str());
    }

    chdir(server->debug.original_cwd.c_str());
    command_execute(server, CommandParser{options.startup_command});
    chdir(getenv("HOME"));

    // Run

    log_info("Running Wayland compositor on WAYLAND_DISPLAY={}", socket);
    wl_display_run(server->display);
}

void cleanup(Server* server)
{
    wl_display_destroy_clients(server->display);
    // TODO: Wait for clients to die properly

    server->listeners.clear();

    ipc_server_cleanup(server);

    wlr_xcursor_manager_destroy(server->cursor_manager);
    wlr_cursor_destroy(server->cursor);
    wlr_allocator_destroy(server->allocator);
    wlr_renderer_destroy(server->renderer);
    wlr_backend_destroy(server->backend);
    wl_display_destroy(server->display);
    wlr_scene_node_destroy(&server->scene->tree.node);
}

constexpr const char* help_prompt = R"(Usage: {} [options] -- [initial command]

Options:
  --xwayland [socket]   Launch xwayland-satellite with given socket (E.g. :0, :1, ...)
  --log-file [path]     Log to file
  --outputs  [count]    Number of outputs to spawn in nested mode
  --ctrl-mod            Use CTRL instead of ALT in nested mode
)";

int main(int argc, char* argv[])
{
    std::vector<std::string_view> args(argv + 1, argv + argc);
    CommandParser cmd{args};

    if (cmd.match("msg")) {
        ipc_client_run(cmd.peek_rest());
        return 0;
    }

    startup_options options = {};

    auto print_usage = [&] {
        std::cout << std::format(help_prompt, PROGRAM_NAME);
        std::exit(0);
    };

    while (cmd) {
        if (cmd.match("--log-file")) {
            options.log_file = cmd.get_string();
        } else if (cmd.match("--xwayland")) {
            options.xwayland_socket = cmd.get_string();
        } else if (cmd.match("--ctrl-mod")) {
            options.ctrl_mod = true;
        } else if (cmd.match("--outputs")) {
            options.additional_outputs = std::max(1, cmd.get_int().value()) - 1;
        } else if (cmd.match("-s")) {
            options.startup_shell_commands.emplace_back(cmd.get_string());
        } else if (cmd.match("--")) {
            options.startup_command = cmd.peek_rest();
            break;
        } else {
            print_usage();
        }
    }

    init_log(LogLevel::trace, WLR_SILENT, options.log_file.empty() ? nullptr : options.log_file.c_str());

    // Init

    Server server = {};
    init(&server, options);

    // Run

    run(&server, options);

    // Closing

    cleanup(&server);

    return 0;
}
