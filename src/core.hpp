#pragma once

#include "pch.hpp"
#include "util.hpp"
#include "log.hpp"
#include "debug.hpp"

// -----------------------------------------------------------------------------

static constexpr uint32_t cursor_size = 24;

static constexpr int   border_width = 2;
static constexpr fvec4 border_color_unfocused = premultiply({ 0.3f, 0.3f, 0.3f, 1.0f });
static constexpr fvec4 border_color_focused   = premultiply({ 0.4f, 0.4f, 1.0f, 1.0f });

static constexpr fvec4 background_color = premultiply({ 0.1f, 0.1f, 0.1f, 1.f });

static constexpr fvec4 zone_color_inital = premultiply({ 0.6f, 0.6f, 0.6f, 0.4f });
static constexpr fvec4 zone_color_select = premultiply({ 0.4f, 0.4f, 1.0f, 0.4f });

static constexpr uint32_t zone_horizontal_zones = 6;
static constexpr uint32_t zone_vertical_zones   = 2;
static constexpr ivec2    zone_selection_leeway = { 200, 200 };
static constexpr struct {
    int left   = 7 + border_width;
    int top    = 7 + border_width;
    int right  = 7 + border_width;
    int bottom = 4 + border_width;
} zone_external_padding;
static constexpr double zone_internal_padding = 4 +  + border_width * 2;

static constexpr const char* keyboard_layout       = "gb";
static constexpr int32_t     keyboard_repeat_rate  = 25;
static constexpr int32_t     keyboard_repeat_delay = 600;

static constexpr double libinput_mouse_speed = -0.66;
static constexpr double pointer_abs_to_rel_speed_multiplier = 5;

// -----------------------------------------------------------------------------

struct OutputConfig
{
    std::optional<ivec2> pos;
    bool primary;
    bool disabled;
};

struct OutputRule { const char* name; OutputConfig config; };
static constexpr OutputRule output_rules[] = {
    { .name = "DP-1", .config = { .pos = ivec2{    0, 0}, .primary = true } },
    { .name = "DP-2", .config = { .pos = ivec2{-3840, 0}                  } },
};

// -----------------------------------------------------------------------------

static constexpr std::string_view output_unaware_clients[] = {
    "xwayland-satellite",
};

// -----------------------------------------------------------------------------

struct WindowQuirks
{
    bool force_pointer_constraint = false;
};
struct WindowRule
{
    const char* app_id;
    const char* title;
    WindowQuirks quirks;
};
static const WindowRule window_rules[] = {
    { .app_id = "Minecraft",  .quirks{.force_pointer_constraint = true} },
    { .app_id = "steam_app_", .quirks{.force_pointer_constraint = true} },
};

// -----------------------------------------------------------------------------

enum class MouseButton {
    Left    = BTN_LEFT,
    Right   = BTN_RIGHT,
    Middle  = BTN_MIDDLE,
    Side    = BTN_SIDE,
    Extra   = BTN_EXTRA,
    Forward = BTN_FORWARD,
    Back    = BTN_BACK,
    Task    = BTN_TASK,
};

enum class ScrollDirection {
    Up,
    Down,
    Left,
    Right,
};

struct Bind
{
    uint32_t modifiers;
    std::variant<xkb_keysym_t, MouseButton, ScrollDirection> action;
    bool release = false;

    constexpr bool operator==(const Bind& o) const
    {
        return modifiers == o.modifiers && action == o.action;
    }
};

struct CommandBind
{
    Bind bind;
    std::vector<std::string> command;
};

// -----------------------------------------------------------------------------

enum class Strata
{
    background,
    floating,
    bottom,
    focused,
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
    move,
    resize,
    zone,
    focus_cycle,
};

struct Toplevel;
struct LayerSurface;
struct CursorSurface;
struct Output;
struct Keyboard;
struct Pointer;
struct Client;

struct Server
{
    ListenerSet listeners;

    wl_display* display;
    wlr_session* session;
    wlr_backend* backend;
    wlr_renderer* renderer;
    wlr_allocator* allocator;

    std::vector<Client*> clients;

    struct {
        std::filesystem::path original_cwd;
        bool is_nested;
        wlr_backend* window_backend;
    } debug;

    wlr_scene* scene;
    EnumMap<wlr_scene_tree*, Strata> layers;
    wlr_output_layout* output_layout;
    wlr_scene_output_layout* scene_output_layout;
    std::vector<Output*> outputs;

    wlr_compositor* compositor;
    wlr_subcompositor* subcompositor;
    wlr_xdg_decoration_manager_v1* xdg_decoration_manager;

    wlr_xdg_foreign_registry* foreign_registry;
    wlr_foreign_toplevel_manager_v1* foreign_toplevel_manager;

    wlr_xdg_shell* xdg_shell;
    wlr_layer_shell_v1* layer_shell;

    wlr_xdg_activation_v1* activation;

    wlr_cursor*           cursor;
    wlr_xcursor_manager*  cursor_manager;
    std::vector<Pointer*> pointers;

    wlr_seat* seat;
    std::vector<Keyboard*> keyboards;

    std::vector<CommandBind> command_binds;

    wl_event_source* ipc_connection_event_source;

    struct {
        wlr_pointer_constraints_v1* pointer_constraints;
        wlr_pointer_constraint_v1* active_constraint;
        wlr_relative_pointer_manager_v1* relative_pointer_manager;
        bool            debug_visual_enabled = false;
        wlr_scene_rect* debug_visual;
        uint32_t        debug_visual_half_extent;
        bool            cursor_is_visible;
    } pointer;

    InteractionMode interaction_mode;

    struct {
        // TODO: This needs to be cleaned up on Toplevel destroy to avoid dangling
        Weak<Toplevel> grabbed_toplevel;
        vec2 grab;
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
        wlr_box initial_zone = {};
        wlr_box final_zone   = {};
        bool moving    = false;
        bool selecting = false;
    } zone;
};

struct Client
{
    Server* server;
    struct wl_client* wl_client;

    ListenerSet listeners;

    pid_t pid;
    uid_t uid;
    gid_t gid;

    std::filesystem::path path;
    std::vector<std::string> cmdline;
    std::string process_name;

    bool is_output_aware = false;

    static Client* from(Server* server, const struct wl_client*);
};

struct Keyboard
{
    ListenerSet listeners;

    Server* server;
    struct wlr_keyboard* wlr_keyboard;
};

struct Pointer
{
    ListenerSet listeners;

    Server* server;

    struct wlr_pointer* wlr_pointer;

    vec2 last_abs_pos;

    static Pointer* from(struct wlr_pointer* pointer) { return pointer ? static_cast<Pointer*>(pointer->data) : nullptr; }
};

struct Output
{
    static Output* from(struct wlr_output* output) { return output ? static_cast<Output*>(output->data) : nullptr; }

    ListenerSet listeners;

    Server* server;
    struct wlr_output* wlr_output;
    wlr_output_layout_output* layout_output;
    wlr_scene_output* scene_output;

    wlr_scene_rect* background;

    wlr_box workarea;

    EnumMap<std::vector<LayerSurface*>, zwlr_layer_shell_v1_layer> layers;

    FrameTimeReporter frame_reporter;
    bool report_stats;

    OutputConfig config;
};

// -----------------------------------------------------------------------------

enum class SurfaceRole
{
    invalid,
    toplevel,
    popup,
    layer_surface,
    subsurface,
};

struct Surface : WeaklyReferenceable
{
    SurfaceRole role = SurfaceRole::invalid;

    ListenerSet listeners;

    Server* server;
    wlr_scene_tree* scene_tree;
    wlr_scene_tree* popup_tree;
    struct wlr_surface* wlr_surface;

    struct {
        bool surface_set;
        Weak<CursorSurface> surface;
        int32_t hotspot_x;
        int32_t hotspot_y;
    } cursor;

    static Surface* from_data(void* data)
    {
        Surface* surface = static_cast<Surface*>(data);
        return (surface && surface->role != SurfaceRole::invalid) ? surface : nullptr;
    }
    static Surface* from(struct wlr_surface* surface) { return surface ? Surface::from_data(surface->data) : nullptr; }
    static Surface* from(    wlr_scene_node* node)    { return node    ? Surface::from_data(node->data)    : nullptr; }

    FrameTimeReporter frame_commit_reporter;
    bool report_stats;
};

struct Subsurface : Surface
{
    static Subsurface* from(Surface* surface)
    {
        return (surface && surface->role == SurfaceRole::subsurface) ? static_cast<Subsurface*>(surface) : nullptr;
    }
    static Subsurface* from(struct wlr_surface* wlr_surface) { return from(Surface::from(wlr_surface)); }
    static Subsurface* from(wlr_scene_node*     node)        { return from(Surface::from(node));        }

    wlr_subsurface* subsurface() const { return wlr_subsurface_try_from_wlr_surface(wlr_surface); }
    Surface* parent() const { return Surface::from(subsurface()->parent); }
};

struct Toplevel : Surface
{
    static Toplevel* from(Surface* surface)
    {
        return (surface && surface->role == SurfaceRole::toplevel) ? static_cast<Toplevel*>(surface) : nullptr;
    }
    static Toplevel* from(struct wlr_surface* wlr_surface) { return from(Surface::from(wlr_surface)); }
    static Toplevel* from(wlr_scene_node*     node)        { return from(Surface::from(node));        }

    wlr_xdg_toplevel* xdg_toplevel() const { return wlr_xdg_toplevel_try_from_wlr_surface(wlr_surface); }

    wlr_scene_rect* border[4];
    struct {
        wlr_xdg_toplevel_decoration_v1* xdg_decoration;
        ListenerSet listeners;
    } decoration;

    wlr_box prev_bounds;

    int anchor_x;
    int anchor_y;
    wlr_edges anchor_edges;

    struct {
        bool enable_throttle_resize = true;

        bool any_pending = false;
        int pending_width;
        int pending_height;

        uint32_t last_resize_serial = 0;
        uint32_t last_commited_serial = 0;
    } resize;

    WindowQuirks quirks;

    wlr_foreign_toplevel_handle_v1* foreign_handle;
    ListenerSet foreign_listeners;

    wlr_xdg_foreign_exported foreign_exported;
};

struct Popup : Surface
{
    static Popup* from(Surface* surface)
    {
        return (surface && surface->role == SurfaceRole::popup) ? static_cast<Popup*>(surface) : nullptr;
    }
    static Popup* from(struct wlr_surface* wlr_surface) { return from(Surface::from(wlr_surface)); }
    static Popup* from(wlr_scene_node*     node)        { return from(Surface::from(node));        }

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

    wlr_layer_surface_v1* wlr_layer_surface() const { return wlr_layer_surface_v1_try_from_wlr_surface(wlr_surface); }

    wlr_scene_layer_surface_v1* scene_layer_surface;
};

struct CursorSurface : Surface
{
    // This listener inherits from Surface with an `invalid` role so that
    // Surface::from(struct wlr_surface*) calls are still always safe to make
};

// ---- Commands ---------------------------------------------------------------

void command_execute(Server*, CommandParser);
bool command_execute_bind(Server*, Bind);

// ---- Process / IPC ----------------------------------------------------------

void ipc_server_init(Server*);
void ipc_server_cleanup(Server*);
void ipc_client_run(std::span<const std::string_view>);

void env_set(Server*, std::string_view name, std::optional<std::string_view> value);

struct SpawnEnvAction { const char* name; const char* value; };
void spawn(Server*, std::string_view file, std::span<const std::string_view> argv, std::span<const SpawnEnvAction> env_actions = {}, const char* wd = nullptr);

// ---- Policy -----------------------------------------------------------------

void set_interaction_mode(Server*, InteractionMode);

void      focus_cycle_begin(Server*, wlr_cursor*);
void      focus_cycle_step( Server*, wlr_cursor*, bool backwards);
Toplevel* focus_cycle_end(  Server*);

bool input_handle_key(   Server*, const wlr_keyboard_key_event&, xkb_keysym_t sym);
bool input_handle_button(Server*, const wlr_pointer_button_event&);
bool input_handle_axis(  Server*, const wlr_pointer_axis_event&);

bool is_mod_down(      Server*, wlr_keyboard_modifier modifiers);
bool is_main_mod_down( Server*);
uint32_t get_modifiers(Server*);

// ---- Client -----------------------------------------------------------------

void client_new(wl_listener*, void*);
void client_destroy(wl_listener*, void*);
bool client_filter_globals(const wl_client*, const wl_global*, void*);

// ---- Keyboard ---------------------------------------------------------------

void keyboard_new(Server*, wlr_input_device*);

void seat_keyboard_focus_change(wl_listener*, void*);

void keyboard_handle_modifiers(wl_listener*, void*);
void keyboard_handle_key(      wl_listener*, void*);
void keyboard_handle_destroy(  wl_listener*, void*);

// ---- Pointer ----------------------------------------------------------------

bool is_cursor_visible(  Server*);
void update_cursor_state(Server*);
void cursor_surface_commit(wl_listener*, void*);
void cursor_surface_destroy(wl_listener*, void*);
bool cursor_surface_is_visible(CursorSurface*);

vec2 get_cursor_pos(Server*);

uint32_t get_num_pointer_buttons_down(Server*);

void process_cursor_resize(Server*);
void process_cursor_motion(Server*, uint32_t time_msecs, wlr_input_device*, vec2 delta, vec2 rel, vec2 rel_unaccel);

void seat_request_set_cursor(      wl_listener*, void*);
void seat_pointer_focus_change(    wl_listener*, void*);

void cursor_motion(         wl_listener*, void*);
void cursor_motion_absolute(wl_listener*, void*);
void cursor_button(         wl_listener*, void*);
void cursor_axis(           wl_listener*, void*);
void cursor_frame(          wl_listener*, void*);

void pointer_new(Server*, wlr_input_device*);

void pointer_destroy(wl_listener*, void*);

// ---- Pointer.Constraints ----------------------------------------------------

void pointer_constraint_new(    wl_listener*, void*);
void pointer_constraint_destroy(wl_listener*, void*);

// ---- Input ------------------------------------------------------------------

void input_new(                 wl_listener*, void*);
void seat_request_set_selection(wl_listener*, void*);
void seat_request_start_drag(   wl_listener*, void*);
void seat_start_drag(           wl_listener*, void*);

// ---- Zone -------------------------------------------------------------------

void zone_init(                 Server*);
void zone_process_cursor_motion(Server*);
bool zone_process_cursor_button(Server*, const wlr_pointer_button_event&);
void zone_begin_selection(      Server*);
void zone_end_selection(        Server*);

wlr_box zone_apply_external_padding(wlr_box);

// ---- Output -----------------------------------------------------------------

Output* get_primary_output(Server*);
bool output_filter_global(Server*, Client*, const wl_global*);
void update_output_states(Server* server);

Output* get_output_at(              Server*, vec2);
Output* get_nearest_output_to_point(Server*, vec2);
Output* get_nearest_output_to_box(  Server*, wlr_box);
Output* get_output_for_surface(     Surface*);

wlr_box output_get_bounds( Output*);
void    output_reconfigure(Output*);

void output_frame(        wl_listener*, void*);
void output_request_state(wl_listener*, void*);
void output_destroy(      wl_listener*, void*);
void output_new(          wl_listener*, void*);
void output_layout_change(wl_listener*, void*);

// ---- Surface ----------------------------------------------------------------

Surface* get_surface_accepting_input_at(Server*, vec2 layout_pos, wlr_surface** p_surface, vec2* surface_pos);
Surface* get_focused_surface(           Server*);

void surface_focus(  Surface*);
void surface_unfocus(Surface*);

wlr_box surface_get_bounds(      Surface*);
wlr_box surface_get_geometry(    Surface*);
wlr_box surface_get_coord_system(Surface*);

void surface_cleanup(Surface*);

void request_activate(wl_listener*, void*);

// ---- Surface.Subsurface -----------------------------------------------------

void subsurface_new(    wl_listener*, void*);
void subsurface_commit( wl_listener*, void*);
void subsurface_destroy(wl_listener*, void*);

// ---- Surface.LayerSurface ---------------------------------------------------

void output_layout_layer(Output*, zwlr_layer_shell_v1_layer);

void layer_surface_commit( wl_listener*, void*);
void layer_surface_unmap(  wl_listener*, void*);
void layer_surface_destroy(wl_listener*, void*);
void layer_surface_new(    wl_listener*, void*);

// ---- Surface.Toplevel -------------------------------------------------------

void toplevel_set_bounds(       Toplevel*, wlr_box, wlr_edges locked_edges = wlr_edges(WLR_EDGE_LEFT | WLR_EDGE_TOP));
void toplevel_set_activated(    Toplevel*, bool active);
bool toplevel_is_fullscreen(    Toplevel*);
void toplevel_set_fullscreen(   Toplevel*, bool fullscreen, Output* output);
void toplevel_update_border(    Toplevel*);
bool toplevel_is_interactable(  Toplevel*);
void toplevel_begin_interactive(Toplevel*, InteractionMode);
void toplevel_close(            Toplevel*);

void toplevel_map(               wl_listener*, void*);
void toplevel_unmap(             wl_listener*, void*);
void toplevel_commit(            wl_listener*, void*);
void toplevel_destroy(           wl_listener*, void*);
void toplevel_request_minimize(  wl_listener*, void*);
void toplevel_request_maximize(  wl_listener*, void*);
void toplevel_request_fullscreen(wl_listener*, void*);
void toplevel_new(               wl_listener*, void*);

// ---- Surface.Toplevel.Decoration --------------------------------------------

void decoration_set_mode(Toplevel*);

void decoration_new(         wl_listener*, void*);
void decoration_request_mode(wl_listener*, void*);
void decoration_destroy(     wl_listener*, void*);

// ---- Surface.Popup ----------------------------------------------------------

void popup_commit( wl_listener*, void*);
void popup_destroy(wl_listener*, void*);
void popup_new(    wl_listener*, void*);

// ---- Debug ------------------------------------------------------------------

std::string surface_to_string(Surface*);
std::string pointer_constraint_to_string(wlr_pointer_constraint_v1*);
std::string client_to_string(Client*);
std::string cursor_surface_to_string(CursorSurface*);
std::string pointer_to_string(Pointer*);
std::string output_to_string(Output*);
