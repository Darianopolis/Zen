#include "core.hpp"

Client* Client::from(Server* server, const struct wl_client* wl_client)
{
    for (Client* client : server->clients) {
        if (client->wl_client == wl_client) return client;
    }
    return nullptr;
}

bool client_filter_globals(const struct wl_client* wl_client, const wl_global* global, void* data)
{
    Server* server = static_cast<Server*>(data);
    Client* client = Client::from(server, wl_client);

    if (&wl_output_interface == wl_global_get_interface(global)) {
        return output_filter_global(server, client, global);
    }

    return true;
}

void client_new(wl_listener* listener, void* data)
{
    Server* server = listener_userdata<Server*>(listener);
    struct wl_client* wl_client = static_cast<struct wl_client*>(data);

    Client* client = new Client{};
    client->server = server;
    client->wl_client = wl_client;

    // Get pid/uid/gid

    wl_client_get_credentials(wl_client, &client->pid, &client->uid, &client->gid);

    // Get process name

    if (std::ifstream file{std::format("/proc/{}/comm", client->pid), std::ios::binary}; file.is_open()) {
        std::getline(file, client->process_name);
    }

    // Get full executable path

    {
        char buf[8192] = {};
        int count = readlink(std::format("/proc/{}/exe", client->pid).c_str(), buf, sizeof(buf));
        if (count >= 0) {
            client->path = std::string_view(buf, count);
        }
    }

#if GET_WL_CLIENT_CMDLINE
    // Get command line

    if (std::ifstream file{std::format("/proc/{}/cmdline", client->pid), std::ios::binary | std::ios::ate}; file.is_open()) {
        char buf[8192] = {};
        file.read(buf, sizeof(buf) - 2);

        for (const char* a = buf; *a; a += strlen(a) + 1) {
            client->cmdline.emplace_back(a);
        }
    }
#endif

    client->is_output_aware = true;
    for (auto allowed : output_unaware_clients) {
        // TODO: Should we allow or deny full output information by default?
        //       Really almost all applications don't actually care about outputs
        //        *OR* only want to see one primary output.
        //       The only exceptions are layer shell components like waybar that need
        //        to produce per-output surfaces, and multi-monitor games
        //       Additionally problematic for XWayland clients, as we can only filter at the
        //        xwayland-satellite level. We'd need multiple xwayland-satellite instances
        //        to filter more granularly.
        if (allowed == client->path.filename()) {
            client->is_output_aware = false;
            break;
        }
    }

    server->clients.emplace_back(client);

    wl_client_add_destroy_listener(wl_client, &client->listeners.listen(nullptr, client, client_destroy)->listener);
}

void client_destroy(wl_listener* listener, void*)
{
    Client* client = listener_userdata<Client*>(listener);

    std::erase(client->server->clients, client);

    log_info("Client disconnected: {}", client_to_string(client));

    delete client;
}
