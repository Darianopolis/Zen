#include "core.hpp"

static constexpr const char* dbus_name        =     DBUS_TLD "." DBUS_NAME "." PROGRAM_NAME;
static constexpr const char* dbus_object_path = "/" DBUS_TLD "/" DBUS_NAME "/" PROGRAM_NAME;

// -----------------------------------------------------------------------------
// Interface: org.freedesktop.Application
//
// See: https://specifications.freedesktop.org/desktop-entry/latest/dbus.html
//
// Test:
//   gdbus call --session
//     --dest         ${DBUS_TLD}.${DBUS_URL}.${PROGRAM_NAME}
//     --object-path /${DBUS_TLD}/${DBUS_URL}/${PROGRAM_NAME}
//     --method org.freedesktop.Application.ActivateAction "foo" "[]" "{}"
// -----------------------------------------------------------------------------

static
i32 dbus_handle_activate(sd_bus_message* m, void* data, sd_bus_error* err)
{
    log_warn("D-Bus :: Activate() - STUB");

    unix_check_ne(sd_bus_reply_method_return(m, ""));

    return 0;
}

static
i32 dbus_handle_open(sd_bus_message* m, void* data, sd_bus_error* err)
{
    log_warn("D-Bus :: Open() - STUB");

    unix_check_ne(sd_bus_reply_method_return(m, ""));

    return 0;
}

static
i32 dbus_handle_activate_action(sd_bus_message* m, void* data, sd_bus_error* err)
{
    const char* action = "?";
    unix_check_ne(sd_bus_message_read(m, "s", &action));

    log_warn("D-Bus :: ActivateAction({}) - STUB", action);

    unix_check_ne(sd_bus_reply_method_return(m, ""));

    return 0;
}

static const sd_bus_vtable dbus_activation_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Activate",       "a{sv}",    "", dbus_handle_activate,        SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Open",           "asa{sv}",  "", dbus_handle_open,            SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ActivateAction", "sava{sv}", "", dbus_handle_activate_action, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

static
void dbus_register_application_interface(Server* server)
{
    const char* interface_name = "org.freedesktop.Application";
    i32 res = sd_bus_add_object_vtable(server->dbus, nullptr, dbus_object_path, interface_name, dbus_activation_vtable, nullptr);
    unix_check_ne(res);
}

// -----------------------------------------------------------------------------

static
i32 dbus_handle_dbus_read(i32 fd, u32 mask, void* data)
{
    auto* server = static_cast<Server*>(data);
    unix_check_ne(sd_bus_process(server->dbus, nullptr));
    return 1;
}

void dbus_init(Server* server)
{
    unix_check_ne(sd_bus_open_user(&server->dbus));
    if (unix_check_ne(sd_bus_request_name(server->dbus, dbus_name, 0), EEXIST) == -EEXIST) {
        log_warn("Failed to acquire D-Bus name, skipping D-Bus client initialization");
        return;
    }

    dbus_register_application_interface(server);

    i32 fd = sd_bus_get_fd(server->dbus);
    server->dbus_source = wl_event_loop_add_fd(wl_display_get_event_loop(server->display), fd, WL_EVENT_READABLE, dbus_handle_dbus_read, server);
}

void dbus_cleanup(Server* server)
{
    if (server->dbus_source) {
        wl_event_source_remove(server->dbus_source);
    }
    sd_bus_unref(server->dbus);
}
