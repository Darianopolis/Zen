#include "core.hpp"
#include "pch.hpp"

void spawn(const char* file, std::span<const std::string_view> argv, std::span<const SpawnEnvAction> env_actions)
{
    std::vector<std::string> argv_str;
    for (std::string_view a : argv) argv_str.emplace_back(a);

    std::vector<char*> argv_cstr;
    for (std::string& s : argv_str) argv_cstr.emplace_back(s.data());
    argv_cstr.emplace_back(nullptr);

    if (fork() == 0) {
        for (const SpawnEnvAction& env_action : env_actions) {
            if (env_action.value) {
                setenv(env_action.name, env_action.value, true);
            } else {
                unsetenv(env_action.name);
            }
        }
        execvp(file, argv_cstr.data());
    }
}
