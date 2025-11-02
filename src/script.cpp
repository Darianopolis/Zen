#include "core.hpp"

static
bool new_boolean_state(bool old_state, std::string_view command)
{
    if (command == "on") return true;
    if (command == "off") return false;
    if (command == "toggle") return !old_state;
    return old_state;
}

static
void script_env_set_globals(Server* server)
{
    auto& lua = server->script.lua;

    lua.set_function("spawn", [server](sol::variadic_args varargs) {
        std::vector<std::string> args;
        for (auto arg : varargs) {
            args.emplace_back(arg.get<std::string>());
        }
        std::vector<std::string_view> argview(args.begin(), args.end());
        spawn(server, argview.front(), argview);
    });

    {
        lua.set_function("bind", [server](std::string_view bind_str, sol::function action) {
            auto bind = bind_from_string(server, bind_str);
            if (!bind) log_error("Failed to parse bind string: {}", bind_str);

            log_info("Creating bind: {}", bind_str);

            bind_register(server, CommandBind {
                .bind = bind.value(),
                .function = [bind = bind.value(), server, bind_str = std::string(bind_str), action = std::move(action)] {
                    log_info("Executing bind: {}", bind_str);
                    try {
                        action();
                    } catch (const sol::error& e) {
                        log_error("Error while executing bind [{}] unregistering", bind_str);
                        bind_erase(server, bind);
                    }
                },
            });
        });

        lua.set_function("unbind", [server](std::optional<std::string_view> bind_str) {
            if (bind_str) {
                auto bind = bind_from_string(server, *bind_str);
                if (!bind) log_error("Failed to parse bind string: {}", *bind_str);
                bind_erase(server, *bind);
            } else {
                server->command_binds.clear();
            }
        });
    }

    {
        sol::table env = lua["env"] = lua.create_table();

        env["launch_dir"] = std::filesystem::current_path().string();

        env.set_function("set", [server](std::string_view name, std::optional<sol::string_view> value) {
            env_set(server, name, value);
        });

        env.set_function("get", [](const char* name) {
            return getenv(name);
        });
    }

    {
        sol::table debug = lua["debug"] = lua.create_table();

        debug.set_function("cursor", [server](std::string_view command) {
            server->pointer.debug_visual_enabled = new_boolean_state(server->pointer.debug_visual_enabled, command);
            log_info("Debug cursor visual: {}", server->pointer.debug_visual_enabled ? "enabled" : "disabled");
            update_cursor_state(server);
        });

        {
            sol::table pointer = debug["pointer"] = lua.create_table();

            pointer.set_function("accel", [server](std::string_view command) {
                server->pointer.debug_accel_rate = new_boolean_state(server->pointer.debug_accel_rate, command);
                log_info("Debug pointer accel: {}", server->pointer.debug_accel_rate);
            });
        }

        {
            sol::table output = debug["output"] = lua.create_table();

            output.set_function("new", [server] {
                if (server->session.window_backend) {
                    wlr_output* output = wlr_wl_output_create(server->session.window_backend);
                    if (output) log_info("Spawning new output: {}", output->name);
                }
            });
        }

        {
            sol::table stats = debug["stats"] = lua.create_table();

            stats.set_function("window", [server](std::string_view command) {
                if (Toplevel* toplevel = Toplevel::from(get_focused_surface(server))) {
                    toplevel->report_stats = new_boolean_state(toplevel->report_stats, command);
                    log_info("{} statistics for {}", toplevel->report_stats ? "Enabling" : "Disabling", surface_to_string(toplevel));
                }
            });

            stats.set_function("output", [server](std::string_view command) {
                if (Output* output = get_nearest_output_to_point(server, get_cursor_pos(server))) {
                    output->report_stats = new_boolean_state(output->report_stats, command);
                    log_info("{} statistics for {}", output->report_stats ? "Enabling" : "Disabling",  output->wlr_output->name);
                }
            });
        }
    }
}

void script_system_init(Server* server)
{
    auto& lua = server->script.lua;

    lua.open_libraries(sol::lib::base, sol::lib::math);

    script_env_set_globals(server);
}

static
sol::environment script_environment_create(Server* server, std::filesystem::path dir)
{
    sol::state& lua = server->script.lua;
    sol::environment e(lua, sol::create, lua.globals());

    dir = std::filesystem::absolute(dir);

    e["source_dir"] = dir.string();

    e.set_function("source", [server, dir](std::string_view path) {
        log_debug("Sourcing [{}] -> {}", path, (dir / path).c_str());
        script_run_file(server, dir / path);
    });

    return e;
}

void script_run(Server* server, std::string_view source, const std::filesystem::path& source_dir)
{
    auto e = script_environment_create(server, source_dir);
    try {
        server->script.lua.script(source, e);
    } catch (const sol::error& e) {
        log_error("Script error: {}", e.what());
    }
}

void script_run_file(Server* server, const std::filesystem::path& script_path)
{
    auto e = script_environment_create(server, script_path.parent_path());
    try {
        server->script.lua.script_file(script_path, e);
    } catch (const sol::error& e) {
        log_error("Script error: {}", e.what());
    }
}
