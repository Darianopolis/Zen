#include "core.hpp"

bool client_is_xwayland_satellite(Client* client)
{
    return client->process_name == "xwayland-satell";
}

struct XWaylandSatelliteSpawnRequest
{
    std::function<void(std::string_view)> callback;
    wl_event_source* event_source;
};

static
i32 xwayland_satellite_handle_read(i32 fd, u32 mask, void* data)
{
    auto* request = static_cast<XWaylandSatelliteSpawnRequest*>(data);

    char buf[4096];
    int len = read(fd, buf, sizeof(buf));
    if (len > 0) {
        // TODO: We shouldn't assume that each read is a complete line
        auto line = std::string_view(buf, len);

        static constexpr std::string_view needle = "Connected to Xwayland on :"sv;
        if (auto idx = line.find(needle); idx != std::string::npos) {
            i32 socket;
            if (std::from_chars(line.data() + idx + needle.size(), line.data() + line.size(), socket)) {
                log_debug("xwayland-satellite connected on socket: [{}]", socket);
                request->callback(std::format(":{}", socket));
            } else {
                log_error("Error parsing xwayland-satellite socket number");
            }
        }
    } else {
        log_warn("xwayland-satellite read failed, removing request object: {}", len);
        wl_event_source_remove(request->event_source);
        close(fd);
        delete request;
    }

    return 1;
}

void xwayland_satellite_spawn(Server* server, const char* requested_socket, std::function<void(std::string_view)> callback)
{
    int p[2];
    unix_check_n1(pipe(p));

    if (fork() == 0) {
        dup2(p[1], STDOUT_FILENO);
        dup2(p[1], STDERR_FILENO);
        execlp("xwayland-satellite", "xwayland-satellite", requested_socket, nullptr);
        _Exit(1);
    }
    close(p[1]);

    auto request = new XWaylandSatelliteSpawnRequest {};
    request->callback = std::move(callback);

    request->event_source = wl_event_loop_add_fd(wl_display_get_event_loop(server->display), p[0], WL_EVENT_READABLE, xwayland_satellite_handle_read, request);
}
