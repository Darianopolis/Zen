#include "core.hpp"

template<typename ...Args>
[[noreturn]] void script_error(std::format_string<Args...> fmt, Args&&... args)
{
    auto message = std::vformat(fmt.get(), std::make_format_args(args...));
    log(LogLevel::error, message);
    throw std::runtime_error(message);
}

struct MetatableBuilder
{
    sol::table metatable;
    sol::table table;

    static constexpr const char* properties_key = "__properties";

    MetatableBuilder(sol::state& lua, auto&& slot): metatable(lua.create_table()), table(lua.create_table())
    {
        metatable["__newindex"] = [](sol::table table, const char* field, sol::object value) {
            auto prop = table[properties_key][field];
            if (prop.is<sol::table>()) {
                prop["set"](value);
            } else {
                script_error("no property with name :{}", field);
            }
        };
        metatable["__index"] = [](sol::table table, const char* field) -> sol::object {
            auto prop = table[properties_key][field];
            if (prop.is<sol::table>()) {
                return prop["get"]();
            } else {
                script_error("no property with name :{}", field);
            }
        };

        table[properties_key] = lua.create_table();

        slot = table;
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
bool script_invoke_safe(auto&& function)
{
    try {
        auto res = function();
        if (!res.valid()) {
            auto _ = sol::error(res);
        }
        return true;
    } catch (const sol::error& e) {
        log_error("Script error: {}", e.what());
        return false;
    }
}

static
fvec4 script_object_to_color(sol::object obj)
{
    fvec4 color = { 0, 0, 0, 1 };
    if (obj.is<sol::table>()) {

        sol::table table = obj.as<sol::table>();
        for (int i = 0; i < 4; ++i) {
            if (table[i + 1].is<float>()) {
                color[i] = table[i + 1].get<float>();
            } else {
                script_error("Error parsing color table, expected float at [{}] got: {}",
                    i + 1, magic_enum::enum_name(table[i + 1].get_type()));
            }
        }
    } else if (obj.is<std::string_view>()) {
        std::string_view s = obj.as<std::string_view>();

        if (s.empty() || s.front() != '#') {
            script_error("Error parsing hex color: Must start with '#'");
        }
        s.remove_prefix(1);
        if (s.size() != 6 && s.size() != 8) {
            script_error("Error parsing hex color: Expected 6 or 8 hex digits, got: {}", s.size());
        }

        auto hex_to_digit = [](char c) -> int {
            if (c >= 'a' && c <= 'f') return 10 + c - 'a';
            if (c >= 'A' && c <= 'F') return 10 + c - 'A';
            if (c >= '0' && c <= '9') return c - '0';
            return -1;
        };

        for (char c : s) if (hex_to_digit(c) < 0) {
            script_error("Error parsing color: Expected hex digit, got {}", c);
        }

        color.r = ((hex_to_digit(s[0]) * 16) + hex_to_digit(s[1])) / 255.0;
        color.g = ((hex_to_digit(s[2]) * 16) + hex_to_digit(s[3])) / 255.0;
        color.b = ((hex_to_digit(s[4]) * 16) + hex_to_digit(s[5])) / 255.0;
        if (s.size() == 8) {
            color.a = ((hex_to_digit(s[6]) << 4) + hex_to_digit(s[7])) / 255.0;
        }

        return color;

    } else {
        script_error("Error parsing color: Expected table got: {}", magic_enum::enum_name(obj.get_type()));
    }
    return color;
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

    sol::table config = lua["config"].get_or_create<sol::table>();

    // Output

    {
        MetatableBuilder output(lua, config["output"]);

        output.add_property("on_add_or_remove", [server](sol::protected_function fn) {
            log_info("Setting output layout add/remove listener");
            server->script.on_output_add_or_remove = [fn = std::move(fn)](Output* output, bool added) {
                log_info("Output added/removed");
                script_invoke_safe([&] {
                    return output
                        ? fn(output->wlr_output->name, added)
                        : fn();
                });
            };
            server->script.on_output_add_or_remove(nullptr, true);
        }, [] { return sol::nil; });
    }

    // Focus cycle

    {
        MetatableBuilder focus_cycle(lua, config["focus_cycle"]);

        focus_cycle.add_property("opacity", [server](float opacity) {
            server->config.layout.focus_cycle_unselected_opacity = opacity;
            log_info("Setting focus_cycle.opacity = {}", opacity);
            scene_reconfigure(server);
        }, [server] { return server->config.layout.focus_cycle_unselected_opacity; });
    }

    // Layout

    {
        {
            MetatableBuilder background(lua, config["background"]);

            background.add_property("color", [server](sol::object color) {
                server->config.layout.background_color = script_object_to_color(color);
                log_info("Setting background.color = {}", glm::to_string(server->config.layout.background_color));
                for (auto* output : server->outputs) {
                    wlr_scene_rect_set_color(output->background, color_to_wlroots(server->config.layout.background_color));
                }
            }, [] { return sol::nil; /* TODO */ });
        }

        {
            MetatableBuilder border(lua, config["border"]);

            border.add_property("width", [server](int width) {
                log_info("Setting border width: {}", width);
                server->config.layout.border_width = width;
                scene_reconfigure(server);
            }, [server] { return server->config.layout.border_width; });

            {
                MetatableBuilder color(lua, border.table["color"]);

                color.add_property("focused", [server](sol::object color) {
                    server->config.layout.border_color_focused = script_object_to_color(color);
                    log_info("Setting border.color.focused = {}", glm::to_string(server->config.layout.border_color_focused));
                }, [] { return sol::nil; /* TODO */ });

                color.add_property("default", [server](sol::object color) {
                    server->config.layout.border_color_unfocused = script_object_to_color(color);
                    log_info("Setting border.color.default = {}", glm::to_string(server->config.layout.border_color_unfocused));
                }, [] { return sol::nil; /* TODO */ });
            }
        }

        // Grid

        {
            MetatableBuilder grid(lua, config["grid"]);

            {
                MetatableBuilder leeway(lua, grid.table["leeway"]);

                leeway.add_property("horizontal", [server](int amount) {
                    log_info("Setting grid.leeway.horizontal = {}", amount);
                    server->config.layout.zone_selection_leeway.x = amount;
                }, [server] { return server->config.layout.zone_selection_leeway.x; });

                leeway.add_property("vertical", [server](int amount) {
                    log_info("Setting grid.leeway.vertical = {}", amount);
                    server->config.layout.zone_selection_leeway.y = amount;
                }, [server] { return server->config.layout.zone_selection_leeway.y; });
            }

            {
                MetatableBuilder color(lua, grid.table["color"]);

                color.add_property("initial", [server](sol::object color) {
                    server->config.layout.zone_color_inital = script_object_to_color(color);
                    log_info("Setting grid.color.initial = {}", glm::to_string(server->config.layout.zone_color_inital));
                }, [] { return sol::nil; /* TODO */ });

                color.add_property("selected", [server](sol::object color) {
                    server->config.layout.zone_color_select = script_object_to_color(color);
                    log_info("Setting grid.color.selected = {}", glm::to_string(server->config.layout.zone_color_select));
                }, [] { return sol::nil; /* TODO */ });
            }

            grid.add_property("width", [server](int width) {
                log_info("Setting grid.width = {}", width);
                server->config.layout.zone_horizontal_zones = width;
            }, [server] { return server->config.layout.zone_horizontal_zones; });

            grid.add_property("height", [server](int height) {
                log_info("Setting grid.height = {}", height);
                server->config.layout.zone_vertical_zones = height;
            }, [server] { return server->config.layout.zone_vertical_zones; });

            {
                MetatableBuilder padding(lua, grid.table["pad"]);

                padding.add_property("inner", [server](uint32_t size) {
                    log_info("Setting grid.pad.inner = {}", size);
                    server->config.layout.zone_internal_padding = size;
                }, [server] { return server->config.layout.zone_internal_padding; });

#define DIRECTIONAL_PADDING(Name) \
                    padding.add_property(#Name, [server](uint32_t size) { \
                        log_info("Setting grid.pad."#Name" = {}", size); \
                        server->config.layout.zone_external_padding.Name = size; \
                    }, [server] { return server->config.layout.zone_external_padding.Name; });

                DIRECTIONAL_PADDING(left)
                DIRECTIONAL_PADDING(top)
                DIRECTIONAL_PADDING(right)
                DIRECTIONAL_PADDING(bottom)

#undef DIRECTIONAL_PADDING
            }
        }
    }

    // Binds

    {
        sol::table binds = config["bind"].get_or_create<sol::table>();

        binds.set_function("clear", [server] {
            server->command_binds.clear();
        });

        sol::table mt = binds[sol::metatable_key].get_or_create<sol::table>();
        mt["__newindex"] = [server](sol::table, std::string_view bind_str, std::optional<sol::protected_function> action) {
            auto bind = bind_from_string(server, bind_str);
            if (!bind) log_error("Failed to parse bind string: {}", bind_str);

            if (action) {
                log_info("Creating bind: {}", bind_str);

                bind_register(server, CommandBind {
                    .bind = bind.value(),
                    .function = [bind = bind.value(), server, bind_str = std::string(bind_str), action = std::move(*action)] {
                        log_info("Executing bind: {}", bind_str);
                        if (!script_invoke_safe(action)) {
                            log_error("Exception while executing bind [{}], unregistering", bind_str);
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
        MetatableBuilder process(lua, lua["process"]);

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
        MetatableBuilder debug(lua, lua["debug"]);

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
            MetatableBuilder pointer(lua, debug.table["pointer"]);

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
    script_invoke_safe([&] {
        return server->script.lua.safe_script(source, e);
    });
}

void script_run_file(Server* server, const std::filesystem::path& script_path)
{
    auto e = script_environment_create(server, script_path.parent_path());
    script_invoke_safe([&] {
        return server->script.lua.safe_script_file(script_path, e);
    });
}
