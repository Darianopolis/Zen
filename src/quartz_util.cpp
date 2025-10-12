#include "quartz.hpp"

#include <vector>
#include <string>

void qz_spawn(const char* file, std::span<const std::string_view> argv)
{
    std::vector<std::string> argv_str;
    for (std::string_view a : argv) argv_str.emplace_back(a);

    std::vector<char*> argv_cstr;
    for (std::string& s : argv_str) argv_cstr.emplace_back(s.data());
    argv_cstr.emplace_back(nullptr);

    if (fork() == 0) {
        execvp(file, argv_cstr.data());
    }
}
