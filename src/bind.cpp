#include "core.hpp"

Modifiers mod_from_string(std::string_view name)
{
    if      (name == "Mod")   { return Modifiers::Mod;   }
    else if (name == "Ctrl")  { return Modifiers::Ctrl;  }
    else if (name == "Shift") { return Modifiers::Shift; }
    else if (name == "Alt")   { return Modifiers::Alt;   }
    else if (name == "Super") { return Modifiers::Super; }

    return {};
}

std::optional<Bind> bind_from_string(Server*, std::string_view bind_string)
{
    Bind bind = {};

    bool has_valid_action = false;

    size_t b = 0;
    for (;;) {
        size_t n = bind_string.find_first_of('+', b);
        auto part = std::string(bind_string.substr(b, n - b));
        if (!part.empty()) {
            if (Modifiers mod = mod_from_string(part); bool(mod)) {
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

void bind_erase(Server* server, Bind bind)
{
    std::erase_if(server->command_binds, [&](const CommandBind& cb) {
        return cb.bind == bind;
    });
}

void bind_register(Server* server, const CommandBind& bind_command)
{
    bind_erase(server, bind_command.bind);
    server->command_binds.emplace_back(bind_command);

    std::sort(server->command_binds.begin(), server->command_binds.end(), [](const CommandBind& l, const CommandBind& r) -> bool {
        return std::popcount(std::to_underlying(l.bind.modifiers)) > std::popcount(std::to_underlying(r.bind.modifiers));
    });
}

bool bind_trigger(Server* server, Bind input_action)
{
    for (auto& cb : server->command_binds) {
        if ((input_action.modifiers & cb.bind.modifiers) == cb.bind.modifiers && cb.bind.action == input_action.action) {
            if (cb.bind.release != input_action.release) {
                // Consume opposite action but do not trigger command
                return true;
            }
            cb.function();
            return true;
        }
    }
    return false;
}
