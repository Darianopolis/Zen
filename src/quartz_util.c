#include "quartz.h"

void qz_spawn(const char* file, char* const argv[])
{
    if (fork() == 0) {
        execvp(file, argv);
    }
}
