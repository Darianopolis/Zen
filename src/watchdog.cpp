#include "core.hpp"

namespace {
    constexpr auto heartbeat_rate_ms  = 100;
    constexpr auto watchdog_ping_rate = 100ms;
    constexpr auto watchdog_timeout   = 1000ms;

    struct {
        wl_event_source* timer;
        std::atomic<std::chrono::steady_clock::time_point> last_heartbeat;
    } watchdog_state;

    void watchdog_dump(std::chrono::steady_clock::duration dur)
    {
        try {
            int id;
            std::filesystem::path path;
            for (id = 1; id <= 99; ++id) {
                path = std::filesystem::path(std::format(PROGRAM_NAME "-watchdog-crash-{}.dump", id));
                if (!std::filesystem::exists(path)) break;
            }
            int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
            std::this_thread::sleep_for(watchdog_ping_rate);
            auto now = std::chrono::steady_clock::now();

            auto dur = now - watchdog_state.last_heartbeat.load();
            if (dur > watchdog_timeout) {
                watchdog_dump(dur);
                return;
            }
        }
    }
}

// -----------------------------------------------------------------------------

static
void watchdog_heartbeat()
{
    auto now = std::chrono::steady_clock::now();
    watchdog_state.last_heartbeat = now;

    wl_event_source_timer_update(watchdog_state.timer, heartbeat_rate_ms);
}

void watchdog_init(Server* server)
{
    watchdog_state.timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->display), [](void*) {
        watchdog_heartbeat();
        return 0;
    }, nullptr);
    watchdog_heartbeat();
    std::thread(watchdog_run).detach();
}

void watchdog_start_shutdown()
{
    if (watchdog_state.timer) {
        wl_event_source_remove(watchdog_state.timer);
    }
    watchdog_heartbeat();
}
