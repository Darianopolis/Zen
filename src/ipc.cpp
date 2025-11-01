#include "core.hpp"

constexpr
std::string to_upper(std::string_view in)
{
    std::string out(in);
    for (char& c : out) c = std::toupper(c);
    return out;
}

static const std::filesystem::path xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
static const std::filesystem::path ipc_socket_dir  = xdg_runtime_dir / PROGRAM_NAME;
static const std::string           ipc_socket_env  = std::format("{}_PROCESS", to_upper(PROGRAM_NAME));

static
sockaddr_un ipc_socket_path_from_name(std::string_view name)
{
    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, (ipc_socket_dir / name).c_str(), sizeof(addr.sun_path));
    return addr;
}

static
void ipc_reap_dead_socket_files()
{
    for (auto& entry : std::filesystem::directory_iterator(ipc_socket_dir)) {
        if (!entry.is_socket()) continue;

        pid_t pid = std::atoi(entry.path().filename().c_str());
        if (pid > 0 && kill(pid, 0) >= 0) {
            continue;
        }

        std::filesystem::remove(entry.path());
    }
}

static
int ipc_open_socket(std::string* name)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

    std::string socket_name = std::to_string(getpid());
    sockaddr_un addr = ipc_socket_path_from_name(socket_name);
    unlink(addr.sun_path);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    listen(fd, 8);

    *name = socket_name;
    return fd;
}

struct MessageConnection
{
    Server* server;
    wl_event_source* source;
    std::filesystem::path cwd;
};

static
int ipc_handle_client_read(int fd, uint32_t /* mask */, void* data)
{
    MessageConnection* conn = static_cast<MessageConnection*>(data);

    std::vector<char> message;
    {
        char buf[4096] = {};
        int read;
        while ((read = recv(fd, buf, sizeof(buf), 0)) > 0) {
            message.append_range(std::string_view(buf, read));
        }
    }
    message.emplace_back('\0');
    message.emplace_back('\0');

    for (const char* a = message.data(); *a; a += strlen(a) + 1) {
        script_run(conn->server, std::string_view(a), conn->cwd);
    }

    close(fd);
    wl_event_source_remove(conn->source);
    delete conn;

    return 0;
}

static
int ipc_handle_socket_accept(int fd, uint32_t /* mask */, void* data)
{
    Server* server = static_cast<Server*>(data);

    int client_fd = accept(fd, nullptr, nullptr);
    if (client_fd < 0) return 0;

    MessageConnection* conn = new MessageConnection {};
    conn->server = server;

    {
        ucred cred;
        socklen_t len = sizeof(cred);
        if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) >= 0) {

            char buf[8192] = {};
            int count = readlink(std::format("/proc/{}/cwd", cred.pid).c_str(), buf, sizeof(buf));
            if (count >= 0) {
                conn->cwd = std::string_view(buf, count);
            }
        }

        if (conn->cwd.empty()) {
            conn->cwd = std::filesystem::current_path();
            log_warn("Could not determine cwd of IPC source, using [{}]", conn->cwd.c_str());
        }

    }

    conn->source = wl_event_loop_add_fd(wl_display_get_event_loop(server->display), client_fd, WL_EVENT_READABLE, ipc_handle_client_read, conn);

    return 0;
}

void ipc_server_init(Server* server)
{
    std::string name;
    std::filesystem::create_directories(ipc_socket_dir);
    ipc_reap_dead_socket_files();
    int fd = ipc_open_socket(&name);
    if (fd >= 0) {
        log_info("Opened IPC socket, setting {}={}", ipc_socket_env, name);
        env_set(server, ipc_socket_env, name);
        server->ipc_connection_event_source = wl_event_loop_add_fd(wl_display_get_event_loop(server->display), fd, WL_EVENT_READABLE, ipc_handle_socket_accept, server);
    }
}

void ipc_server_cleanup(Server* server)
{
    if (server->ipc_connection_event_source)
        wl_event_source_remove(server->ipc_connection_event_source);
}

void ipc_client_run(std::span<const std::string_view> args)
{
    const char* socket_name = getenv(ipc_socket_env.c_str());
    auto addr = ipc_socket_path_from_name(socket_name);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("connect");
        return;
    }

    std::vector<char> buf;
    for (auto arg : args) {
        buf.append_range(arg);
        buf.emplace_back('\0');
    }
    buf.emplace_back('\0');

    send(fd, buf.data(), buf.size(), 0);

    close(fd);
}
