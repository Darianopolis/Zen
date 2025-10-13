#pragma once

#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>

#include <xkbcommon/xkbcommon.h>

#include "wlroots.hpp"
#include "util.hpp"

#include <libinput.h>

#include <vector>

// -----------------------------------------------------------------------------

#ifndef QZ_NOISY_RESIZE
# define QZ_NOISY_RESIZE 0
#endif

// -----------------------------------------------------------------------------

static constexpr int      qz_border_width = 2;
static constexpr qz_color qz_border_color_unfocused = { 0.3f, 0.3f, 0.3f, 1.0f };
static constexpr qz_color qz_border_color_focused   = { 0.4f, 0.4f, 1.0f, 1.0f };

static constexpr qz_color qz_background_color = { 0.1f, 0.1f, 0.1f, 1.f };

static constexpr qz_color qz_zone_color_inital = { 0.6f, 0.6f, 0.6f, 0.4f };
static constexpr qz_color qz_zone_color_select = { 0.4f, 0.4f, 1.0f, 0.4f };

static constexpr uint32_t qz_zone_horizontal_zones = 6;
static constexpr uint32_t qz_zone_vertical_zones   = 2;
static constexpr qz_point qz_zone_zone_selection_leeway = { 200, 200 };
static constexpr double   qz_zone_external_padding_ltrb[] = { 7 + qz_border_width, 7 + qz_border_width, 7 + qz_border_width, 7 + qz_border_width };
static constexpr double   qz_zone_internal_padding = 4 +  + qz_border_width * 2;

struct qz_monitor_rule { const char* name; int x, y; };
static constexpr std::array qz_monitor_rules = {
    qz_monitor_rule { .name = "DP-1", .x =     0, .y = 0 },
    qz_monitor_rule { .name = "DP-2", .x = -3840, .y = 0 },
};

static constexpr const char* qz_keyboard_layout       = "gb";
static constexpr int32_t     qz_keyboard_repeat_rate  = 25;
static constexpr int32_t     qz_keyboard_repeat_delay = 600;

static constexpr double qz_libinput_mouse_speed = -0.66;

// -----------------------------------------------------------------------------

enum class qz_cursor_mode
{
    passthrough,
    pressed,
    move,
    resize,
    zone,
};

struct qz_toplevel;
struct qz_output;
struct qz_keyboard;

struct qz_server
{
    qz_listener_set listeners;

    struct wl_display* wl_display;
    wlr_backend* backend;
    wlr_renderer* renderer;
    wlr_allocator* allocator;
    wlr_scene* scene;
    wlr_scene_output_layout* scene_output_layout;
    wlr_compositor* compositor;
    wlr_subcompositor* subcompositor;
    wlr_xdg_decoration_manager_v1* xdg_decoration_manager;

    wlr_xdg_shell* xdg_shell;
    std::vector<qz_toplevel*> toplevels;

    wlr_cursor* cursor;
    wlr_xcursor_manager* cursor_manager;

    wlr_seat* seat;
    std::vector<qz_keyboard*> keyboards;

    qz_cursor_mode cursor_mode;

    struct {
        qz_toplevel* grabbed_toplevel;
        double grab_x, grab_y;
        wlr_box grab_geobox;
        uint32_t resize_edges;
    } movesize;

    qz_toplevel* focused_toplevel;

    wlr_output_layout* output_layout;
    std::vector<qz_output*> outputs;

    uint32_t modifier_key;

    wlr_scene_tree* drag_icon_parent;

    struct {
        wlr_box selection;
        wlr_scene_rect* selector;
        qz_box initial_zone = {};
        qz_box final_zone   = {};
        bool moving    = false;
        bool selecting = false;
    } zone;
};

struct qz_output
{
    qz_listener_set listeners;

    qz_server* server;
    struct wlr_output* wlr_output;
    wlr_scene_output* scene_output;

    wlr_scene_rect* background;
};

enum class qz_client_type
{
    toplevel,
    popup,
};

struct qz_client
{
    qz_client_type type;

    qz_server* server;
    wlr_scene_tree* scene_tree;
    wlr_xdg_surface* xdg_surface;
};

struct qz_toplevel : qz_client
{
    qz_listener_set listeners;

    wlr_xdg_toplevel* xdg_toplevel;

    wlr_scene_rect* border[4];
    struct {
        wlr_xdg_toplevel_decoration_v1* xdg_decoration;
        qz_listener_set listeners;
    } decoration;

    wlr_box prev_bounds;

    struct {
        bool enable_throttle_resize = true;

        bool any_pending = false;
        int pending_width;
        int pending_height;

        uint32_t last_resize_serial = 0;
        uint32_t last_commited_serial = 0;
    } resize;
};

struct qz_popup : qz_client
{
    qz_listener_set listeners;

    wlr_xdg_popup* xdg_popup;
};

struct qz_keyboard
{
    qz_listener_set listeners;

    qz_server* server;
    struct wlr_keyboard* wlr_keyboard;
};

// ---- Policy ----

void qz_cycle_focus_immediate(qz_server*, wlr_cursor*, bool backwards);

bool     qz_handle_keybinding(qz_server*, xkb_keysym_t);
uint32_t qz_get_modifiers(    qz_server*);
bool     qz_is_main_mod_down( qz_server*);

// ---- Keyboard ----

void qz_keyboard_handle_modifiers(wl_listener*, void*);
void qz_keyboard_handle_key(      wl_listener*, void*);
void qz_keyboard_handle_destroy(  wl_listener*, void*);

// ---- Mouse ----

void qz_reset_cursor_mode(    qz_server*);
void qz_process_cursor_resize(qz_server*);
void qz_process_cursor_motion(qz_server*, uint32_t time_msecs);

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

// ---- Zone ----

void qz_zone_init(                 qz_server*);
void qz_zone_process_cursor_motion(qz_server*);
bool qz_zone_process_cursor_button(qz_server*, wlr_pointer_button_event*);
void qz_zone_begin_selection(      qz_server*);
void qz_zone_end_selection(        qz_server*);

// ---- Output ----

qz_output* qz_get_output_at(        qz_server*, double x, double y);
qz_output* qz_get_output_for_client(qz_client*);

wlr_box qz_output_get_bounds( qz_output*);
void    qz_output_reconfigure(qz_output*);

void qz_output_frame(               wl_listener*, void*);
void qz_output_request_state(       wl_listener*, void*);
void qz_output_destroy(             wl_listener*, void*);
void qz_server_new_output(          wl_listener*, void*);
void qz_server_output_layout_change(wl_listener*, void*);

// ---- Client ----

wlr_box qz_client_get_bounds(      qz_client*);
wlr_box qz_client_get_geometry(    qz_client*);
wlr_box qz_client_get_coord_system(qz_client*);

// ---- Client.Toplevel ----

void         qz_toplevel_focus(           qz_toplevel*);
void         qz_toplevel_unfocus(         qz_toplevel*);
void         qz_toplevel_resize(          qz_toplevel*, int width, int height);
void         qz_toplevel_set_bounds(      qz_toplevel*, wlr_box);
void         qz_toplevel_set_activated(   qz_toplevel*, bool active);
bool         qz_toplevel_wants_fullscreen(qz_toplevel*);
void         qz_toplevel_set_fullscreen(  qz_toplevel*, bool fullscreen);
wlr_surface* qz_toplevel_get_surface(     qz_toplevel*);
bool         qz_toplevel_is_interactable( qz_toplevel*);

qz_toplevel* qz_get_toplevel_at(qz_server*, double lx, double ly, wlr_surface**, double *sx, double *sy);

void qz_toplevel_begin_interactive(qz_toplevel*, qz_cursor_mode, uint32_t edges);

void qz_toplevel_map(               wl_listener*, void*);
void qz_toplevel_unmap(             wl_listener*, void*);
void qz_toplevel_commit(            wl_listener*, void*);
void qz_toplevel_destroy(           wl_listener*, void*);
void qz_toplevel_request_maximize(  wl_listener*, void*);
void qz_toplevel_request_fullscreen(wl_listener*, void*);
void qz_server_new_toplevel(        wl_listener*, void*);

// ---- Client.Toplevel.Decoration ----

void qz_decoration_set_mode(qz_toplevel*);

void qz_decoration_new(         wl_listener*, void*);
void qz_decoration_request_mode(wl_listener*, void*);
void qz_decoration_destroy(     wl_listener*, void*);

// ---- Client.Popup ----

void qz_popup_commit(    wl_listener*, void*);
void qz_popup_destroy(   wl_listener*, void*);
void qz_server_new_popup(wl_listener*, void*);
