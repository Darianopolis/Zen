#include "core.hpp"

// -----------------------------------------------------------------------------

static
std::filesystem::path find_on_path(std::string_view in)
{
    std::string_view path = getenv("PATH");

    size_t b = 0;
    for (;;) {
        size_t n = path.find_first_of(":", b);
        auto part = path.substr(b, n - b);

        auto path = std::filesystem::path(part) / in;
        if (std::filesystem::exists(path)) {
            return path;
        }

        if (n == std::string::npos) break;
        b = n + 1;
    }

    return {};
}

void spawn(Server*, std::string_view file, std::span<const std::string_view> argv, std::span<const SpawnEnvAction> env_actions, const char* wd)
{
    std::vector<std::string> argv_str;
    for (std::string_view a : argv) argv_str.emplace_back(a);

    std::vector<char*> argv_cstr;
    for (std::string& s : argv_str) argv_cstr.emplace_back(s.data());
    argv_cstr.emplace_back(nullptr);

    log_info("Spawning process [{}] args {}", file, argv);

    auto path = find_on_path(file);
    if (path.empty()) {
        log_error("  Could not find on path");
        return;
    }

    log_debug("  Full path: {}", path.c_str());

    if (access(path.c_str(), X_OK) != 0) {
        log_error("  File is not executable");
        return;
    }

    if (fork() == 0) {
        if (wd) {
            chdir(wd);
        }
        for (const SpawnEnvAction& env_action : env_actions) {
            if (env_action.value) {
                setenv(env_action.name, env_action.value, true);
            } else {
                unsetenv(env_action.name);
            }
        }
        execv(path.c_str(), argv_cstr.data());
        _Exit(0);
    }
}

void env_set(Server* server, std::string_view name, std::optional<std::string_view> value)
{
    if (value) {
        setenv(std::string(name).c_str(), std::string(*value).c_str(), true);
    } else {
        unsetenv(std::string(name).c_str());
    }

    if (!server->debug.is_nested) {
        spawn(server, "systemctl", {"systemctl", "--user", "import-environment", name});
    }
}

// -----------------------------------------------------------------------------

static
wlr_keyboard_modifier mod_from_string(Server* server, std::string_view name)
{
    if      (name == "Mod")   { return wlr_keyboard_modifier(server->main_modifier); }
    else if (name == "Ctrl")  { return WLR_MODIFIER_CTRL;  }
    else if (name == "Shift") { return WLR_MODIFIER_SHIFT; }
    else if (name == "Alt")   { return WLR_MODIFIER_ALT;   }
    else if (name == "Super") { return WLR_MODIFIER_LOGO;  }

    return {};
}

static
std::optional<Bind> bind_from_string(Server* server, std::string_view bind_string)
{
    Bind bind = {};

    bool has_valid_action = false;

    size_t b = 0;
    for (;;) {
        size_t n = bind_string.find_first_of('+', b);
        auto part = std::string(bind_string.substr(b, n - b));
        if (!part.empty()) {
            if (wlr_keyboard_modifier mod = mod_from_string(server, part)) {
                bind.modifiers |= mod;
            }
            else if (part == "ScrollUp")    { bind.action = ScrollDirection::Up;       }
            else if (part == "ScrollDown")  { bind.action = ScrollDirection::Down;     }
            else if (part == "ScrollLeft")  { bind.action = ScrollDirection::Left;     }
            else if (part == "ScrollRight") { bind.action = ScrollDirection::Right;    }
            else {
                bool release = false;
                if (part.ends_with('^')) {
                    release = true;
                    part = part.substr(0, part.size() - 1);
                }
                xkb_keysym_t keysym = xkb_keysym_from_name(part.c_str(), XKB_KEYSYM_NO_FLAGS);
                if (keysym != XKB_KEY_NoSymbol) {
                    bind.action = keysym;
                    bind.release = release;
                    has_valid_action = true;
                } else {
                    log_error("Bind part '{}' not recognized", part);
                    return std::nullopt;
                }
            }
        }

        if (n == std::string::npos) break;
        b = n + 1;
    }

    if (has_valid_action) {
        return bind;
    } else {
        log_error("Bind has no valid trigger action");
        return std::nullopt;
    }
}

static
void command_erase_bind(Server* server, Bind bind)
{
    std::erase_if(server->command_binds, [&](const CommandBind& cb) {
        return cb.bind == bind;
    });
}

static
void command_unbind(Server* server, CommandParser cmd)
{
    auto bind_string = cmd.get_string();

    log_info("Removing bind: {}", bind_string);

    auto bind = bind_from_string(server, bind_string);

    if (bind) command_erase_bind(server, *bind);
}

static
void command_bind(Server* server, CommandParser cmd)
{
    auto bind_string = cmd.get_string();

    log_info("Creating bind: {} -> {}", bind_string, cmd.peek_rest());

    CommandBind bind_command = {};

    auto bind = bind_from_string(server, bind_string);
    if (!bind) return;

    bind_command.bind = *bind;

    for (auto& arg : cmd.peek_rest()) {
        bind_command.command.emplace_back(arg);
    }

    command_erase_bind(server, bind_command.bind);
    server->command_binds.emplace_back(bind_command);

    std::sort(server->command_binds.begin(), server->command_binds.end(), [](const CommandBind& l, const CommandBind& r) -> bool {
        return std::popcount(l.bind.modifiers) > std::popcount(r.bind.modifiers);
    });
}

bool command_execute_bind(Server* server, Bind input_action)
{
    for (auto& cb : server->command_binds) {
        if ((input_action.modifiers & cb.bind.modifiers) == cb.bind.modifiers && cb.bind.action == input_action.action) {
            if (cb.bind.release != input_action.release) {
                // Consume opposite action but do not trigger command
                return true;
            }
            std::vector<std::string_view> args(cb.command.begin(), cb.command.end());
            CommandParser cmd{args};
            command_execute(server, cmd);
            return true;
        }
    }
    return false;
}

bool command_new_boolean_state(bool old_state, std::string_view command)
{
    if (command == "on") return true;
    if (command == "off") return false;
    if (command == "toggle") return !old_state;
    return old_state;
}

void command_execute(Server* server, CommandParser cmd)
{
    if (cmd.match("spawn")) {
        spawn(server, cmd.peek(), cmd.peek_rest());

    } else if (cmd.match("bind")) {
        command_bind(server, cmd);
    } else if (cmd.match("unbind")) {
        command_unbind(server, cmd);
    } else if (cmd.match("unbind-all")) {
        server->command_binds.clear();

    } else if (cmd.match("env")) {
        if (cmd.match("set")) {
            auto name = cmd.get_string();
            auto value = cmd.get_string();
            env_set(server, name, value);
        } else if (cmd.match("unset")) {
            env_set(server, cmd.get_string(), std::nullopt);
        }

    } else if (cmd.match("debug")) {
        if (cmd.match("cursor")) {
            server->pointer.debug_visual_enabled = command_new_boolean_state(server->pointer.debug_visual_enabled, cmd.get_string());
            log_info("Debug cursor visual: {}", server->pointer.debug_visual_enabled ? "enabled" : "disabled");
            update_cursor_state(server);

        } else if (cmd.match("output")) {
            if (cmd.match("new")) {
                if (server->debug.window_backend) {
                    wlr_output* output = wlr_wl_output_create(server->debug.window_backend);
                    if (output) log_info("Spawning new output: {}", output->name);
                }
            }

        } else if (cmd.match("stats")) {
            if (cmd.match("window")) {
                if (Toplevel* toplevel = Toplevel::from(get_focused_surface(server))) {
                    toplevel->report_stats = command_new_boolean_state(toplevel->report_stats, cmd.get_string());
                    log_info("{} statistics for {}", toplevel->report_stats ? "Enabling" : "Disabling", surface_to_string(toplevel));
                }

            } else if (cmd.match("output")) {
                if (Output* output = get_nearest_output_to_point(server, get_cursor_pos(server))) {
                    output->report_stats = command_new_boolean_state(output->report_stats, cmd.get_string());
                    log_info("{} statistics for {}", output->report_stats ? "Enabling" : "Disabling",  output->wlr_output->name);
                }
            }
        }
    }
}
