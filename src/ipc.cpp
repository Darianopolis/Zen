#include "core.hpp"

static const std::filesystem::path xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
static const std::filesystem::path ipc_socket_dir  = xdg_runtime_dir / PROGRAM_NAME;
static const std::string           ipc_socket_env  = ascii_to_upper(PROGRAM_NAME) + "_PROCESS";

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
i32 ipc_open_socket(std::string* name)
{
    i32 fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

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

static
std::optional<MessageHeader> ipd_read_message_header(i32 fd, i32 flags)
{
    MessageHeader header;
    ssize_t read = recv(fd, &header, sizeof(header), flags | MSG_NOSIGNAL);
    if (read == sizeof(header)) return header;
    return std::nullopt;
}

static
bool ipc_read_string(i32 fd, const MessageHeader& header, std::string& out)
{
    out.resize(header.size);
    ssize_t read = recv(fd, out.data(), out.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
    return read == header.size;
}

void ipc_send_string(i32 fd, MessageType type, std::string_view str)
{
    MessageHeader header {
        .type = type,
        .size = u32(str.size()),
    };
    send(fd, &header, sizeof(header), MSG_NOSIGNAL);
    send(fd, str.data(), str.size(),  MSG_NOSIGNAL);
}

static
i32 ipc_handle_client_read(i32 fd, u32 /* mask */, void* data)
{
    MessageConnection* conn = static_cast<MessageConnection*>(data);

    {
        log_set_message_sink(conn);
        defer { log_set_message_sink(nullptr); };

        std::string arg;
        while (auto header = ipd_read_message_header(conn->fd, MSG_DONTWAIT)) {
            if (header->type == MessageType::Argument && ipc_read_string(fd, *header, arg)) {
                script_run(conn->server, arg, conn->cwd);
            }
        }
    }

    close(fd);
    wl_event_source_remove(conn->source);
    delete conn;

    return 0;
}

static
i32 ipc_handle_socket_accept(i32 fd, u32 /* mask */, void* data)
{
    Server* server = static_cast<Server*>(data);

    i32 client_fd = accept(fd, nullptr, nullptr);
    if (client_fd < 0) return 0;

    MessageConnection* conn = new MessageConnection {};
    conn->server = server;
    conn->fd = client_fd;

    log_trace("connection, fd = {}", conn->fd);

    {
        ucred cred;
        socklen_t len = sizeof(cred);
        if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) >= 0) {

            char buf[8192] = {};
            i32 count = readlink(std::format("/proc/{}/cwd", cred.pid).c_str(), buf, sizeof(buf));
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
    i32 fd = ipc_open_socket(&name);
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

i32 ipc_client_run(std::span<const std::string_view> args)
{
    const char* socket_name = getenv(ipc_socket_env.c_str());
    auto addr = ipc_socket_path_from_name(socket_name);

    i32 fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("connect");
        return EXIT_FAILURE;
    }

    for (auto arg : args) {
        ipc_send_string(fd, MessageType::Argument, arg);
    }

    std::string msg;
    while (auto header = ipd_read_message_header(fd, 0)) {
        switch (header->type) {
            case MessageType::StdOut: if (ipc_read_string(fd, *header, msg)) std::cout << msg; break;
            case MessageType::StdErr: if (ipc_read_string(fd, *header, msg)) std::cerr << msg; break;
            default:
        }
    }

    close(fd);

    return EXIT_SUCCESS;
}
