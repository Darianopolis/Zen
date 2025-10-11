#ifndef QUARTZ_H
#define QUARTZ_H

#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>

#include "quartz_wlroots.h"

#include <xkbcommon/xkbcommon.h>

#define QZ_LISTEN(Event, Listener, Handler) wl_signal_add(&(Event), ((Listener).notify = (Handler), &(Listener)))
#define QZ_UNLISTEN(Listener)               qz_unlisten(&(Listener))

inline
void qz_unlisten(struct wl_listener* listener)
{
    if (listener->notify) {
        wl_list_remove(&listener->link);
        listener->notify = nullptr;
    } else {
        wlr_log(WLR_INFO, "QUARTZ_TRACE: unlisten called on non-activated listener");
    }
}

enum qz_cursor_mode
{
    QZ_CURSOR_PASSTHROUGH,
    QZ_CURSOR_MOVE,
    QZ_CURSOR_RESIZE,
};

struct qz_server
{
    struct wl_display* wl_display;
    struct wlr_backend* backend;
    struct wlr_renderer* renderer;
    struct wlr_allocator* allocator;
    struct wlr_scene* scene;
    struct wlr_scene_output_layout* scene_output_layout;
    struct wlr_compositor* compositor;
    struct wlr_subcompositor* subcompositor;

#ifdef QZ_XWAYLAND
    struct wlr_xwayland* xwayland;
    struct wl_listener xwayland_ready;
    struct wl_listener new_xwayland_surface;
#endif

    struct wlr_xdg_shell* xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_list toplevels;

    struct wlr_cursor* cursor;
    struct wlr_xcursor_manager* cursor_manager;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    struct wlr_seat* seat;
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener pointer_focus_change;
    struct wl_listener request_set_selection;
    struct wl_listener request_start_drag;
    struct wl_listener start_drag;
    struct wl_list keyboards;

    enum qz_cursor_mode cursor_mode;
    struct qz_toplevel* grabbed_toplevel;
    double grab_x, grab_y;
    struct wlr_box grab_geobox;
    uint32_t resize_edges;

    struct qz_toplevel* focused_toplevel;

    struct wlr_output_layout* output_layout;
    struct wl_list outputs;
    struct wl_listener new_output;
    struct wl_listener output_layout_change;

    uint32_t modifier_key;

    // Parent node in the scene for attaching drag icons to
    struct wlr_scene_tree* drag_icon_parent;
};

struct qz_output
{
    struct wl_list link;
    struct qz_server* server;
    struct wlr_output* wlr_output;
    struct wlr_scene_output* scene_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;

    struct wlr_scene_rect* background;
};

// enum qz_client_type
// {
//     QZ_CLIENT_XDG_SHELL,
// #ifdef QZ_XWAYLAND
//     QZ_CLIENT_XWAYLAND,
// #endif
// };

struct qz_toplevel
{
    // enum qz_client_type type;

    struct wl_list link;
    struct qz_server* server;
    struct wlr_scene_tree* scene_tree;

    struct wlr_xdg_toplevel* xdg_toplevel;
#ifdef QZ_XWAYLAND
    struct wlr_xwayland_surface* xwayland_surface;
#endif

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;

    struct wl_listener destroy;

    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;

#ifdef QZ_XWAYLAND
    struct wl_listener x_activate;
    struct wl_listener x_associate;
    struct wl_listener x_dissociate;
    struct wl_listener x_configure;
    struct wl_listener x_set_hints;
#endif

    struct wlr_box prev_bounds;
};

struct qz_popup
{
    struct wlr_xdg_popup* xdg_popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct qz_keyboard
{
    struct wl_list link;
    struct qz_server* server;
    struct wlr_keyboard* wlr_keyboard;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

// ---- Util ----

void qz_spawn(const char* file, const char* const argv[]);

auto qz_ptr(auto&& value) { return &value; }

// ---- Policy ----

// Here we handle compositor keybindings. this is when the compositor is processing keys, rather than passing them on to the client for its own processing
bool qz_handle_keybinding(struct qz_server*, xkb_keysym_t);
bool qz_is_main_mod_down(struct qz_server* server);

// ---- Keyboard ----

void qz_keyboard_handle_modifiers(struct wl_listener*, void*);
void qz_keyboard_handle_key(struct wl_listener*, void*);
void qz_keyboard_handle_destroy(struct wl_listener*, void*);

// ---- Mouse ----

// This event is raised by the seat when a client provides a cursor image
void qz_seat_request_set_cursor(struct wl_listener*, void*);
// This event is raised when the pointer focus is changed, including when the client is closed.
// We set the cursor image to its default if trarget surface is NULL
void qz_seat_pointer_focus_change(struct wl_listener*, void*);
// Reset the cursor mode to passthrough
void qz_reset_cursor_mode(struct qz_server*);
// Resizing the grabbed toplevel can be a little bit complicated, because we could be resizing from any corner or edge.
// This not only resizes the toplevel on one or two axes, but can also move the toplevel if you resize
// From the top or left edges (or top-left corner)
void qz_process_cursor_resize(struct qz_server*);
// If the mode is non-passthrough, delegate to those functions, otherwise find the toplevel under the pointer and send the event along
void qz_process_cursor_motion(struct qz_server*, uint32_t time);
// This event is forwarded by the cursor when a pointer emits a relative pointer motion event (i.e. a delta)
void qz_server_cursor_motion(struct wl_listener*, void*);
// This event is forwarded by the cursor when a pointer emits an absolute motion event, from 0..1 on each axis. This happens, for example, when wlroots
// is running under a wayland window rather than kms+drm, and you move the mouse over the window. you could enter the window from any edge,
// so we have to warp the mouse there. there is also some hardware which emits these events
void qz_server_cursor_motion_absolute(struct wl_listener*, void*);
// This event is forwarded by the cursor when a pointer emits a button event
void qz_server_cursor_button(struct wl_listener*, void*);
// This event is forwarded by the cursor when a pointer emits an axis event, for example when you move the scroll wheel.
void qz_server_cursor_axis(struct wl_listener*, void*);
// This event is forwarded by the cursor when a pointer emits a frame event.
// Frame events are sent after regular pointer events to group multiple events together. for instance
// two axiss events may happen at the same time, in which case a frame event won't be sent inbetween
void qz_server_cursor_frame(struct wl_listener*, void*);

// ---- Input ----

// This event is raised by the seat when a client wants to set the selection, usually when the user copies something.
// wlroots allows compositors to ignore such requests if they so choose, but in quartzs we always honor
void qz_seat_request_set_selection(struct wl_listener*, void*);

void qz_seat_request_start_drag(struct wl_listener* listener, void* data);
void qz_seat_start_drag(struct wl_listener* listener, void* data);

void qz_server_new_keyboard(struct qz_server*, struct wlr_input_device*);
// We don't do anything special with pointers. all of our pointer handling is proxied through wlr_cursor.
// On another compositor, you might take this opportunity to do libinput configuration on the device to set acceleration, etc..
void qz_server_new_pointer(struct qz_server*, struct wlr_input_device*);
// This event is raised by the backend when a new input device becomes available
void qz_server_new_input(struct wl_listener*, void*);

// ---- Output ----

qz_output* qz_get_output_at(struct qz_server* server, double x, double y);
wlr_box qz_output_get_bounds(struct qz_output*);
qz_output* qz_get_output_for_toplevel(struct qz_toplevel*);

void qz_output_frame(struct wl_listener*, void*);
void qz_output_request_state(struct wl_listener*, void*);
void qz_output_destroy(struct wl_listener*, void*);
void qz_server_new_output(struct wl_listener*, void*);
void qz_server_output_layout_change(struct wl_listener*, void*);

// ---- Toplevel ----

// This function only deals with keyboard focus
void qz_focus_toplevel(struct qz_toplevel*);

void qz_toplevel_set_bounds(struct qz_toplevel*, wlr_box);
wlr_box qz_toplevel_get_bounds(struct qz_toplevel*);
bool qz_toplevel_wants_fullscreen(struct qz_toplevel*);
void qz_toplevel_set_fullscreen(struct qz_toplevel*, bool fullscreen);

// This returns the topmost node in the scene at the given layout coords.
// We only care about surface nodes as we are specifically looking for a surface in the surface tree of a quartz_client
struct qz_toplevel* qz_desktop_toplevel_at(struct qz_server*, double lx, double ly, struct wlr_surface**, double *sx, double *sy);

struct wlr_surface* qz_toplevel_get_surface(struct qz_toplevel* toplevel);
bool qz_toplevel_is_unmanaged(struct qz_toplevel* toplevel);

// Called when the surface is mapped, or ready to display on-screen
void qz_xdg_toplevel_map(struct wl_listener*, void*);
// Called when the surface is unmapped, and should no longer be shown
void qz_xdg_toplevel_unmap(struct wl_listener*, void*);
// Called when a new surface state is committed
void qz_xdg_toplevel_commit(struct wl_listener*, void*);
// Called wehn the xdg_toplevel is destroyed
void qz_xdg_toplevel_destroy(struct wl_listener*, void*);
// Sets up an interactive move or resize operation, where the compositor stops propogating pointer events to clients and instead consumes them itself, to move or resize windows
void qz_begin_interactive(struct qz_toplevel*, enum qz_cursor_mode, uint32_t edges);
void qz_xdg_toplevel_request_maximize(struct wl_listener*, void*);
void qz_xdg_toplevel_request_fullscreen(struct wl_listener*, void*);
void qz_server_new_xdg_toplevel(struct wl_listener*, void*);

// ---- Popup ----

// Called when a new surface state is committed
void qz_xdg_popup_commit(struct wl_listener*, void*);
void qz_xdg_popup_destroy(struct wl_listener*, void*);
void qz_server_new_xdg_popup(struct wl_listener*, void*);

// ---- XWayland ---

#ifdef QZ_XWAYLAND
void qz_init_xwayland(struct qz_server* server);
void qz_destroy_xwayland(struct qz_server* server);

void qz_xwayland_ready(struct wl_listener*, void*);
void qz_new_xwayland_surface(struct wl_listener*, void*);
#endif

#endif
