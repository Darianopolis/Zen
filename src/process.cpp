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

// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------

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
