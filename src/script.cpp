#include "core.hpp"

struct MetatableBuilder
{
    sol::table metatable;
    sol::table table;

    static constexpr const char* properties_key = "_properties_";

    MetatableBuilder(sol::state& lua): metatable(lua.create_table()), table(lua.create_table())
    {
        metatable["__newindex"] = [](sol::table table, const char* field, sol::object value) {        table[properties_key][field]["set"](value); };
        metatable["__index"]    = [](sol::table table, const char* field)                    { return table[properties_key][field]["get"]();      };

        table[properties_key] = lua.create_table();
    }

    ~MetatableBuilder()
    {
        table[sol::metatable_key] = metatable;
    }

    void add_property(const char* name, auto set, auto get)
    {
        auto props = table[properties_key][name].get_or_create<sol::table>();
        props["set"] = set;
        props["get"] = get;
    }
};

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

    sol::table config = lua["config"].get_or_create<sol::table>();

    // Binds

    {
        sol::table binds = config["bind"].get_or_create<sol::table>();

        binds.set_function("clear", [server] {
            server->command_binds.clear();
        });

        sol::table mt = binds[sol::metatable_key].get_or_create<sol::table>();
        mt["__newindex"] = [server](sol::table, std::string_view bind_str, std::optional<sol::function> action) {
            auto bind = bind_from_string(server, bind_str);
            if (!bind) log_error("Failed to parse bind string: {}", bind_str);

            if (action) {
                log_info("Creating bind: {}", bind_str);

                bind_register(server, CommandBind {
                    .bind = bind.value(),
                    .function = [bind = bind.value(), server, bind_str = std::string(bind_str), action = std::move(*action)] {
                        log_info("Executing bind: {}", bind_str);
                        try {
                            action();
                        } catch (const sol::error& e) {
                            log_error("Error while executing bind [{}] unregistering", bind_str);
                            bind_erase(server, bind);
                        }
                    },
                });
            } else {
                bind_erase(server, *bind);
            }
        };
    }

    // Process

    {
        MetatableBuilder process(lua);
        lua["process"] = process.table;

        process.add_property("cwd",
            [](const char* cwd) { chdir(cwd); },
            [] { return std::filesystem::current_path().string(); });
    }

    // Environment

    {
        sol::table env = lua["env"].get_or_create<sol::table>();

        sol::table mt = env[sol::metatable_key].get_or_create<sol::table>();
        mt["__newindex"] = [server](sol::table, std::string_view name, std::optional<sol::string_view> value) {
            env_set(server, name, value);
        };
        mt["__index"] = [](sol::table, const char* field) {
            return getenv(field);
        };
    }

    // Debug

    {
        MetatableBuilder debug(lua);
        lua["debug"] = debug.table;

        // Testing

        debug.table.set_function("force_timeout", [] {
            std::this_thread::sleep_for(10s);
        });

        // Cursor

        debug.add_property("cursor", [server](bool state) {
            server->pointer.debug_visual_enabled = state;
            log_info("Debug cursor visual: {}", server->pointer.debug_visual_enabled ? "enabled" : "disabled");
            update_cursor_state(server);
        }, [server] { return server->pointer.debug_visual_enabled; });

        // Pointer

        {
            MetatableBuilder pointer(lua);
            debug.table["pointer"] = pointer.table;

            pointer.add_property("accel", [server](bool state) {
                server->pointer.debug_accel_rate = state;
                log_info("Debug pointer acceleration: {}", state ? "enabled" : "disabled");
            }, [server] { return server->pointer.debug_accel_rate; });
        }

        // Damage

        debug.add_property("damage", [server](bool state) {
            server->scene->WLR_PRIVATE.debug_damage_option = state ? WLR_SCENE_DEBUG_DAMAGE_HIGHLIGHT : WLR_SCENE_DEBUG_DAMAGE_NONE;
            log_info("Debug damage visual: {}", state ? "enabled" : "disabled");
        }, [server] {
            return server->scene->WLR_PRIVATE.debug_damage_option == WLR_SCENE_DEBUG_DAMAGE_HIGHLIGHT;
        });

        // Outputs

        {
            sol::table output = debug.table["output"].get_or_create<sol::table>();

            output.set_function("new", [server] {
                if (server->session.window_backend) {
                    wlr_output* output = wlr_wl_output_create(server->session.window_backend);
                    if (output) log_info("Spawning new output: {}", output->name);
                }
            });
        }

        // Statistics

        {
            MetatableBuilder stats(lua);
            debug.table["stats"] = stats.table;

            stats.add_property("window", [server](bool state) {
                if (Toplevel* toplevel = Toplevel::from(get_focused_surface(server))) {
                    toplevel->report_stats = state;
                    log_info("{} statistics for {}", state ? "Enabling" : "Disabling", surface_to_string(toplevel));
                }
            }, [server] {
                Toplevel* toplevel = Toplevel::from(get_focused_surface(server));
                return toplevel && toplevel->report_stats;
            });

            stats.add_property("output", [server](bool state) {
                if (Output* output = get_nearest_output_to_point(server, get_cursor_pos(server))) {
                    output->report_stats = state;
                    log_info("{} statistics for {}", state ? "Enabling" : "Disabling",  output->wlr_output->name);
                }
            }, [server] {
                Output* output = get_nearest_output_to_point(server, get_cursor_pos(server));
                return output && output->report_stats;
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
