#include "core.hpp"

// -----------------------------------------------------------------------------

void spawn(Server*, std::string_view file, std::span<const std::string_view> argv, std::span<const SpawnEnvAction> env_actions, const char* wd)
{
    std::vector<std::string> argv_str;
    for (std::string_view a : argv) argv_str.emplace_back(a);

    std::vector<char*> argv_cstr;
    for (std::string& s : argv_str) argv_cstr.emplace_back(s.data());
    argv_cstr.emplace_back(nullptr);

    log_info("Spawning process [{}] args {}", file, argv);

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
        std::string file_str{file};
        execvp(file_str.c_str(), argv_cstr.data());
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
Bind bind_from_string(Server* server, std::string_view bind_string)
{
    Bind bind = {};

    size_t b = 0;
    for (;;) {
        size_t n = bind_string.find_first_of('+', b);
        auto part = std::string(bind_string.substr(b, n - b));
        if (!part.empty()) {
            if      (part == "Mod")         { bind.modifiers |= server->main_modifier; }
            else if (part == "Ctrl")        { bind.modifiers |= WLR_MODIFIER_CTRL;     }
            else if (part == "Shift")       { bind.modifiers |= WLR_MODIFIER_SHIFT;    }
            else if (part == "Alt")         { bind.modifiers |= WLR_MODIFIER_ALT;      }
            else if (part == "Super")       { bind.modifiers |= WLR_MODIFIER_LOGO;     }
            else if (part == "ScrollUp")    { bind.action = ScrollDirection::Up;       }
            else if (part == "ScrollDown")  { bind.action = ScrollDirection::Down;     }
            else if (part == "ScrollLeft")  { bind.action = ScrollDirection::Left;     }
            else if (part == "ScrollRight") { bind.action = ScrollDirection::Right;    }
            else {
                bind.action = xkb_keysym_from_name(part.c_str(), XKB_KEYSYM_NO_FLAGS);
            }
        }

        if (n == std::string::npos) break;
        b = n + 1;
    }

    return bind;
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

    command_erase_bind(server, bind);
}

static
void command_bind(Server* server, CommandParser cmd)
{
    auto bind_string = cmd.get_string();

    log_info("Creating bind: {} -> {}", bind_string, cmd.peek_rest());

    CommandBind bind_command = {};

    bind_command.bind = bind_from_string(server, bind_string);

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
            std::vector<std::string_view> args(cb.command.begin(), cb.command.end());
            CommandParser cmd{args};
            command_execute(server, cmd);
            return true;
        }
    }
    return false;
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
    }
}
