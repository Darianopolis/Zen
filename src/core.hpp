#pragma once

#include "pch.hpp"
#include "util.hpp"
#include "log.hpp"

// -----------------------------------------------------------------------------

#define NOISY_RESIZE 0

// -----------------------------------------------------------------------------

static constexpr uint32_t cursor_size = 24;

static constexpr int   border_width = 2;
static constexpr Color border_color_unfocused = { 0.3f, 0.3f, 0.3f, 1.0f };
static constexpr Color border_color_focused   = { 0.4f, 0.4f, 1.0f, 1.0f };

static constexpr Color background_color = { 0.1f, 0.1f, 0.1f, 1.f };

static constexpr Color zone_color_inital = { 0.6f, 0.6f, 0.6f, 0.4f };
static constexpr Color zone_color_select = { 0.4f, 0.4f, 1.0f, 0.4f };

static constexpr uint32_t zone_horizontal_zones = 6;
static constexpr uint32_t zone_vertical_zones   = 2;
static constexpr Point    zone_selection_leeway = { 200, 200 };
static constexpr struct {
    int left   = 7 + border_width;
    int top    = 7 + border_width;
    int right  = 7 + border_width;
    int bottom = 4 + border_width;
} zone_external_padding_ltrb;
static constexpr double zone_internal_padding = 4 +  + border_width * 2;

struct OutputRule { const char* name; int x, y; };
static constexpr OutputRule output_rules[] = {
    { .name = "DP-1", .x =     0, .y = 0 },
    { .name = "DP-2", .x = -3840, .y = 0 },
};

static constexpr const char* keyboard_layout       = "gb";
static constexpr int32_t     keyboard_repeat_rate  = 25;
static constexpr int32_t     keyboard_repeat_delay = 600;

static constexpr double libinput_mouse_speed = -0.66;

// -----------------------------------------------------------------------------

enum class Strata
{
    background,
    bottom,
    floating,
    top,
    overlay,
    debug,
};

constexpr Strata strata_from_wlr(zwlr_layer_shell_v1_layer layer)
{
    switch (layer) {
        case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND: return Strata::background;
        case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:     return Strata::bottom;
        case ZWLR_LAYER_SHELL_V1_LAYER_TOP:        return Strata::top;
        case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:    return Strata::overlay;
    }
}

enum class InteractionMode
{
    passthrough,
    pressed,
    move,
    resize,
    zone,
    focus_cycle,
};

struct Toplevel;
struct LayerSurface;
struct Output;
struct Keyboard;

struct Server
{
    ListenerSet listeners;

    wl_display* display;
    wlr_backend* backend;
    wlr_renderer* renderer;
    wlr_allocator* allocator;

    struct {
        bool ignore_mouse_constraints = false;
    } debug;

    wlr_scene* scene;
    EnumMap<wlr_scene_tree*, Strata> layers;
    wlr_output_layout* output_layout;
    wlr_scene_output_layout* scene_output_layout;

    wlr_compositor* compositor;
    wlr_subcompositor* subcompositor;
    wlr_xdg_decoration_manager_v1* xdg_decoration_manager;

    wlr_xdg_shell* xdg_shell;
    wlr_layer_shell_v1* layer_shell;

    bool cursor_visible = true;
    wlr_cursor* cursor;
    wlr_xcursor_manager* cursor_manager;

    wlr_seat* seat;
    std::vector<Keyboard*> keyboards;

    struct {
        wlr_pointer_constraints_v1* pointer_constraints;
        wlr_pointer_constraint_v1* active_constraint;
        wlr_relative_pointer_manager_v1* relative_pointer_manager;
        wlr_scene_rect* debug_visual;
    } pointer;

    InteractionMode interaction_mode;

    struct {
        Toplevel* grabbed_toplevel;
        Point grab;
        wlr_box grab_bounds;
        uint32_t resize_edges;
    } movesize;

    uint32_t main_modifier;
    xkb_keysym_t main_modifier_keysym_left;
    xkb_keysym_t main_modifier_keysym_right;

    wlr_scene_tree* drag_icon_parent;

    struct {
        wlr_box selection;
        wlr_scene_rect* selector;
        Box initial_zone = {};
        Box final_zone   = {};
        bool moving    = false;
        bool selecting = false;
    } zone;
};

struct Keyboard
{
    ListenerSet listeners;

    Server* server;
    struct wlr_keyboard* wlr_keyboard;
};

struct Output
{
    static Output* from(struct wlr_output* output) { return output ? static_cast<Output*>(output->data) : nullptr; }

    ListenerSet listeners;

    Server* server;
    struct wlr_output* wlr_output;
    wlr_scene_output* scene_output;

    wlr_scene_rect* background;

    wlr_box workarea;

    EnumMap<std::vector<LayerSurface*>, zwlr_layer_shell_v1_layer> layers;
};

// -----------------------------------------------------------------------------

enum class SurfaceRole
{
    toplevel,
    popup,
    layer_surface,
};

struct Surface
{
    SurfaceRole role;

    Server* server;
    wlr_scene_tree* scene_tree;
    wlr_scene_tree* popup_tree;
    struct wlr_surface* wlr_surface;

    static Surface* from(struct wlr_surface* surface) { return surface ? static_cast<Surface*>(surface->data) : nullptr; }
    static Surface* from(    wlr_scene_node* node)    { return node    ? static_cast<Surface*>(node->data)    : nullptr; }
};

struct Toplevel : Surface
{
    static Toplevel* from(Surface* surface)
    {
        return (surface && surface->role == SurfaceRole::toplevel) ? static_cast<Toplevel*>(surface) : nullptr;
    }
    static Toplevel* from(struct wlr_surface* wlr_surface) { return from(Surface::from(wlr_surface)); }
    static Toplevel* from(wlr_scene_node*     node)        { return from(Surface::from(node));        }

    ListenerSet listeners;

    wlr_xdg_toplevel* xdg_toplevel() const { return wlr_xdg_toplevel_try_from_wlr_surface(wlr_surface); }

    wlr_scene_rect* border[4];
    struct {
        wlr_xdg_toplevel_decoration_v1* xdg_decoration;
        ListenerSet listeners;
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

struct Popup : Surface
{
    static Popup* from(Surface* surface)
    {
        return (surface && surface->role == SurfaceRole::popup) ? static_cast<Popup*>(surface) : nullptr;
    }
    static Popup* from(struct wlr_surface* wlr_surface) { return from(Surface::from(wlr_surface)); }
    static Popup* from(wlr_scene_node*     node)        { return from(Surface::from(node));        }

    ListenerSet listeners;

    wlr_xdg_popup* xdg_popup() const { return wlr_xdg_popup_try_from_wlr_surface(wlr_surface); }
};

struct LayerSurface : Surface
{
    static LayerSurface* from(Surface* surface)
    {
        return (surface && surface->role == SurfaceRole::layer_surface) ? static_cast<LayerSurface*>(surface) : nullptr;
    }
    static LayerSurface* from(struct wlr_surface* wlr_surface) { return from(Surface::from(wlr_surface)); }
    static LayerSurface* from(wlr_scene_node*     node)        { return from(Surface::from(node));        }

    ListenerSet listeners;

    wlr_layer_surface_v1* wlr_layer_surface() const { return wlr_layer_surface_v1_try_from_wlr_surface(wlr_surface); }

    wlr_scene_layer_surface_v1* scene_layer_surface;
};

// ---- Policy -----------------------------------------------------------------

void reset_interaction_state(Server*);

void focus_cycle_begin(Server*, wlr_cursor*);
void focus_cycle_step( Server*, wlr_cursor*, bool backwards);
void focus_cycle_end(  Server*);

bool     handle_keybinding(Server*, xkb_keysym_t);
uint32_t get_modifiers(    Server*);
bool     is_main_mod_down( Server*);

// ---- Keyboard ---------------------------------------------------------------

void seat_keyboard_focus_change(wl_listener*, void*);

void keyboard_handle_modifiers(wl_listener*, void*);
void keyboard_handle_key(      wl_listener*, void*);
void keyboard_handle_destroy(  wl_listener*, void*);

// ---- Pointer ----------------------------------------------------------------

void process_cursor_resize(Server*);
void process_cursor_motion(Server*, uint32_t time_msecs, wlr_input_device *, double dx, double dy, double dx_unaccel, double dy_unaccel);

void seat_reset_cursor(Server*);
void seat_request_set_cursor(      wl_listener*, void*);
void seat_pointer_focus_change(    wl_listener*, void*);

void server_cursor_motion(         wl_listener*, void*);
void server_cursor_motion_absolute(wl_listener*, void*);
void server_cursor_button(         wl_listener*, void*);
void server_cursor_axis(           wl_listener*, void*);
void server_cursor_frame(          wl_listener*, void*);

// ---- Pointer.Constraints ----------------------------------------------------

void server_pointer_constraint_new(wl_listener*, void*);
void server_pointer_constraint_destroy(wl_listener*, void*);

// ---- Input ------------------------------------------------------------------

void server_new_keyboard(Server*, wlr_input_device*);
void server_new_pointer( Server*, wlr_input_device*);

void server_new_input(          wl_listener*, void*);
void seat_request_set_selection(wl_listener*, void*);
void seat_request_start_drag(   wl_listener*, void*);
void seat_start_drag(           wl_listener*, void*);

// ---- Zone -------------------------------------------------------------------

void zone_init(                 Server*);
void zone_process_cursor_motion(Server*);
bool zone_process_cursor_button(Server*, wlr_pointer_button_event*);
void zone_begin_selection(      Server*);
void zone_end_selection(        Server*);

wlr_box zone_apply_external_padding(wlr_box);

// ---- Output -----------------------------------------------------------------

Output* get_output_at(              Server*, Point);
Output* get_nearest_output_to_point(Server*, Point);
Output* get_nearest_output_to_box(  Server*, wlr_box);
Output* get_output_for_surface(     Surface*);

wlr_box output_get_bounds( Output*);
void    output_reconfigure(Output*);

void output_frame(               wl_listener*, void*);
void output_request_state(       wl_listener*, void*);
void output_destroy(             wl_listener*, void*);
void server_new_output(          wl_listener*, void*);
void server_output_layout_change(wl_listener*, void*);

// ---- Surface ----------------------------------------------------------------

Surface* get_surface_at(     Server* server, double lx, double ly, wlr_surface** p_surface, double *p_sx, double *p_sy);
Surface* get_focused_surface(Server*);

void surface_focus(  Surface*);
void surface_unfocus(Surface*, bool force);

wlr_box surface_get_bounds(      Surface*);
wlr_box surface_get_geometry(    Surface*);
wlr_box surface_get_coord_system(Surface*);

// ---- Surface.LayerSurface ---------------------------------------------------

void server_new_layer_surface(wl_listener*, void*);
void output_layout_layer(Output*, zwlr_layer_shell_v1_layer);

// ---- Surface.Toplevel -------------------------------------------------------

void toplevel_resize(          Toplevel*, int width, int height);
void toplevel_set_bounds(      Toplevel*, wlr_box);
void toplevel_set_activated(   Toplevel*, bool active);
void toplevel_set_fullscreen(  Toplevel*, bool fullscreen);
void toplevel_update_border(   Toplevel*);
bool toplevel_is_interactable( Toplevel*);

void walk_toplevels_front_to_back(Server* server, bool(*for_each)(void*, Toplevel*), void* for_each_data);
void walk_toplevels_back_to_front(Server* server, bool(*for_each)(void*, Toplevel*), void* for_each_data);

void toplevel_begin_interactive(Toplevel*, InteractionMode, uint32_t edges);

void toplevel_map(               wl_listener*, void*);
void toplevel_unmap(             wl_listener*, void*);
void toplevel_commit(            wl_listener*, void*);
void toplevel_destroy(           wl_listener*, void*);
void toplevel_request_minimize(  wl_listener*, void*);
void toplevel_request_maximize(  wl_listener*, void*);
void toplevel_request_fullscreen(wl_listener*, void*);
void server_new_toplevel(        wl_listener*, void*);

// ---- Surface.Toplevel.Decoration --------------------------------------------

void decoration_set_mode(Toplevel*);

void decoration_new(         wl_listener*, void*);
void decoration_request_mode(wl_listener*, void*);
void decoration_destroy(     wl_listener*, void*);

// ---- Surface.Popup ----------------------------------------------------------

void popup_commit(    wl_listener*, void*);
void popup_destroy(   wl_listener*, void*);
void server_new_popup(wl_listener*, void*);
