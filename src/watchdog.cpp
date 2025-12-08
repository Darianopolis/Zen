#include "core.hpp"

namespace {
    constexpr auto watchdog_ping_interval_ms = 500;
    constexpr auto watchdog_check_interval   = 500ms;
    constexpr auto watchdog_timeout          = 5000ms;

    struct {
        wl_event_source* timer;
        std::atomic<std::chrono::steady_clock::time_point> last_ping;
    } watchdog_state;

    void watchdog_dump(std::chrono::steady_clock::duration dur)
    {
        try {
            i32 id;
            std::filesystem::path path;
            for (id = 1; id <= 99; ++id) {
                path = std::filesystem::path(std::format(PROGRAM_NAME "-watchdog-crash-{}.dump", id));
                if (!std::filesystem::exists(path)) break;
            }
            i32 fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        } catch (...) {}

        std::cout << "--------------------------------------------------------------------------------\n";
        std::cout << std::format("---- WATCHDOG TIMEOUT ({})\n", duration_to_string(dur));
        std::cout << "--------------------------------------------------------------------------------\n\n";

        kill(getpid(), SIGABRT);
    }

    void watchdog_run()
    {
        for (;;) {
            std::this_thread::sleep_for(watchdog_check_interval);
            auto now = std::chrono::steady_clock::now();

            auto dur = now - watchdog_state.last_ping.load();
            if (dur > watchdog_timeout) {
                watchdog_dump(dur);
                return;
            }
        }
    }
}

// -----------------------------------------------------------------------------

static
void watchdog_ping()
{
    auto now = std::chrono::steady_clock::now();
    watchdog_state.last_ping = now;

    wl_event_source_timer_update(watchdog_state.timer, watchdog_ping_interval_ms);
}

void watchdog_init(Server* server)
{
    watchdog_state.timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->display), [](void*) {
        watchdog_ping();
        return 0;
    }, nullptr);
    watchdog_ping();
    std::thread(watchdog_run).detach();
}

void watchdog_start_shutdown()
{
    if (watchdog_state.timer) {
        wl_event_source_remove(watchdog_state.timer);
    }
    watchdog_ping();
}
