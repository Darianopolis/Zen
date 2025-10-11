#include "quartz.hpp"

#include <vector>
#include <string>

void qz_spawn(const char* file, const char* const argv[])
{
    std::vector<std::string> argv_str;
    for (const char* const* a = argv; *a ; ++a) argv_str.push_back(*a);

    std::vector<char*> argv_cstr;
    for (std::string& s : argv_str) argv_cstr.emplace_back(s.data());
    argv_cstr.emplace_back(nullptr);

    if (fork() == 0) {
        execvp(file, argv_cstr.data());
    }
}
