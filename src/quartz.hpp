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

#include <xkbcommon/xkbcommon.h>

#include "quartz_wlroots.h"
#include "quartz_util.hpp"

enum class qz_cursor_mode
{
    passthrough,
    move,
    resize,
};

struct qz_toplevel;

struct qz_server
{
    qz_listener_set listeners;

    wl_display* wl_display;
    wlr_backend* backend;
    wlr_renderer* renderer;
    wlr_allocator* allocator;
    wlr_scene* scene;
    wlr_scene_output_layout* scene_output_layout;
    wlr_compositor* compositor;
    wlr_subcompositor* subcompositor;

    wlr_xdg_shell* xdg_shell;
    wl_list toplevels;

    wlr_cursor* cursor;
    wlr_xcursor_manager* cursor_manager;

    wlr_seat* seat;
    wl_list keyboards;

    qz_cursor_mode cursor_mode;
    qz_toplevel* grabbed_toplevel;
    double grab_x, grab_y;
    wlr_box grab_geobox;
    uint32_t resize_edges;

    qz_toplevel* focused_toplevel;

    wlr_output_layout* output_layout;
    wl_list outputs;

    uint32_t modifier_key;

    // Parent node in the scene for attaching drag icons to
    wlr_scene_tree* drag_icon_parent;
};

struct qz_output
{
    qz_listener_set listeners;

    wl_list link;
    qz_server* server;
    wlr_output* wlr_output;
    wlr_scene_output* scene_output;

    wlr_scene_rect* background;
};

struct qz_toplevel
{
    qz_listener_set listeners;

    wl_list link;
    qz_server* server;
    wlr_scene_tree* scene_tree;

    wlr_xdg_toplevel* xdg_toplevel;

    wlr_box prev_bounds;
};

struct qz_popup
{
    qz_listener_set listeners;

    wlr_xdg_popup* xdg_popup;
};

struct qz_keyboard
{
    qz_listener_set listeners;

    wl_list link;
    qz_server* server;
    wlr_keyboard* wlr_keyboard;
};

// ---- Policy ----

bool qz_handle_keybinding(qz_server*, xkb_keysym_t);
bool qz_is_main_mod_down( qz_server*);

// ---- Keyboard ----

void qz_keyboard_handle_modifiers(wl_listener*, void*);
void qz_keyboard_handle_key(      wl_listener*, void*);
void qz_keyboard_handle_destroy(  wl_listener*, void*);

// ---- Mouse ----

void qz_reset_cursor_mode(    qz_server*);
void qz_process_cursor_resize(qz_server*);
void qz_process_cursor_motion(qz_server*, uint32_t time);

void qz_seat_request_set_cursor(      wl_listener*, void*);
void qz_seat_pointer_focus_change(    wl_listener*, void*);
void qz_server_cursor_motion(         wl_listener*, void*);
void qz_server_cursor_motion_absolute(wl_listener*, void*);
void qz_server_cursor_button(         wl_listener*, void*);
void qz_server_cursor_axis(           wl_listener*, void*);
void qz_server_cursor_frame(          wl_listener*, void*);

// ---- Input ----

void qz_server_new_keyboard(qz_server*, wlr_input_device*);
void qz_server_new_pointer( qz_server*, wlr_input_device*);

void qz_server_new_input(          wl_listener*, void*);
void qz_seat_request_set_selection(wl_listener*, void*);
void qz_seat_request_start_drag(   wl_listener*, void*);
void qz_seat_start_drag(           wl_listener*, void*);

// ---- Output ----

qz_output* qz_get_output_at(qz_server*, double x, double y);

wlr_box qz_output_get_bounds(qz_output*);

qz_output* qz_get_output_for_toplevel(qz_toplevel*);

void qz_output_frame(               wl_listener*, void*);
void qz_output_request_state(       wl_listener*, void*);
void qz_output_destroy(             wl_listener*, void*);
void qz_server_new_output(          wl_listener*, void*);
void qz_server_output_layout_change(wl_listener*, void*);

// ---- Toplevel ----

void         qz_focus_toplevel(           qz_toplevel*);
void         qz_toplevel_set_bounds(      qz_toplevel*, wlr_box);
wlr_box      qz_toplevel_get_bounds(      qz_toplevel*);
bool         qz_toplevel_wants_fullscreen(qz_toplevel*);
void         qz_toplevel_set_fullscreen(  qz_toplevel*, bool fullscreen);
wlr_surface* qz_toplevel_get_surface(     qz_toplevel*);

qz_toplevel* qz_get_toplevel_at(qz_server*, double lx, double ly, wlr_surface**, double *sx, double *sy);

void qz_toplevel_begin_interactive(qz_toplevel*, qz_cursor_mode, uint32_t edges);

void qz_toplevel_map(               wl_listener*, void*);
void qz_toplevel_unmap(             wl_listener*, void*);
void qz_toplevel_commit(            wl_listener*, void*);
void qz_toplevel_destroy(           wl_listener*, void*);
void qz_toplevel_request_maximize(  wl_listener*, void*);
void qz_toplevel_request_fullscreen(wl_listener*, void*);
void qz_server_new_xdg_toplevel(    wl_listener*, void*);

// ---- Popup ----

void qz_xdg_popup_commit(    wl_listener*, void*);
void qz_xdg_popup_destroy(   wl_listener*, void*);
void qz_server_new_xdg_popup(wl_listener*, void*);

#endif
