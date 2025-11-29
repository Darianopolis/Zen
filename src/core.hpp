#pragma once

#include "pch.hpp"
#include "util.hpp"
#include "log.hpp"

// -----------------------------------------------------------------------------

static constexpr uint32_t cursor_size = 24;

struct LayoutConfig
{
    int border_width = 1;

    fvec4 background_color = { 0, 0, 0, 1 };

    float focus_cycle_unselected_opacity = 0.0;

    fvec4 border_color_unfocused = { 1, 0, 1, 0.3 };
    fvec4 border_color_focused   = { 1, 0, 1, 1.0 };

    uint32_t zone_horizontal_zones = 2;
    uint32_t zone_vertical_zones = 2;
    ivec2 zone_selection_leeway = { 1, 1 };
    struct {
        int left = 1, top = 1, right = 1, bottom = 1;
    } zone_external_padding;
    int zone_internal_padding = 1;

    fvec4 zone_color_inital = { 1, 0, 1, 0.3 };
    fvec4 zone_color_select = { 1, 0, 1, 0.6 };
};

// -----------------------------------------------------------------------------

static constexpr bool keyboard_default_numlock_state = true;

static constexpr const char* keyboard_layout       = "gb";
static constexpr int32_t     keyboard_repeat_rate  =  25;
static constexpr int32_t     keyboard_repeat_delay = 600;

struct PointerAccelConfig
{
    double offset;
    double rate;
    double multiplier;
};

static constexpr PointerAccelConfig pointer_accel     = { 2.0, 0.05, 0.3 };
static constexpr PointerAccelConfig pointer_rel_accel = { 2.0, 0.05, 1.0 };

static constexpr double pointer_abs_to_rel_speed_multiplier = 5;

static constexpr uint32_t pointer_modifier_button = BTN_SIDE;

// -----------------------------------------------------------------------------

enum class Modifiers : uint32_t
{
    Mod   = 1 << 0,
    Super = 1 << 1,
    Ctrl  = 1 << 2,
    Shift = 1 << 3,
    Alt   = 1 << 4,
};
DECORATE_FLAG_ENUM(Modifiers)

// -----------------------------------------------------------------------------

enum class MouseButton : uint32_t
{
    Left    = BTN_LEFT,
    Right   = BTN_RIGHT,
    Middle  = BTN_MIDDLE,
    Side    = BTN_SIDE,
    Extra   = BTN_EXTRA,
    Forward = BTN_FORWARD,
    Back    = BTN_BACK,
    Task    = BTN_TASK,
};

enum class ScrollDirection : uint32_t
{
    Up,
    Down,
    Left,
    Right,
};

struct Bind
{
    Modifiers modifiers;
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
    std::function<void()> function;
};

enum class InteractionMode : uint32_t
{
    passthrough,
    move,
    resize,
    zone,
    focus_cycle,
};

struct Surface;
struct DragIcon;
struct Toplevel;
struct Popup;
struct LayerSurface;
struct CursorSurface;
struct Output;
struct Keyboard;
struct Pointer;
struct Client;

struct ColorRect
{
    wlr_box box;
    fvec4 color;
};

struct Server
{
    struct {
        LayoutConfig layout;
    } config;

    ListenerSet listeners;

    struct {
        sol::state lua;
        std::filesystem::path current_script_dir;

        std::function<void(Output*, bool)> on_output_add_or_remove = [](Output*, bool){};
    } script;

    wl_display* display;
    struct wlr_session* wlr_session;
    wlr_backend* backend;
    wlr_renderer* renderer;
    wlr_allocator* allocator;

    std::vector<Client*> clients;

    std::vector<Toplevel*> toplevels;
    std::vector<LayerSurface*> layer_surfaces;

    Weak<Toplevel> toplevel_under_cursor;
    Weak<Surface> surface_under_cursor;

    struct {
        std::filesystem::path home_dir;
        bool is_nested;
        wlr_backend* window_backend;
    } session;

    wlr_output_layout* output_layout;
    std::vector<Output*> outputs;
    wlr_output_manager_v1* output_manager;

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
        uint32_t        debug_visual_half_extent;
        fvec4           debug_visual_color;
        bool            cursor_is_visible;
        bool            debug_accel_rate = false;
    } pointer;

    InteractionMode interaction_mode;

    struct {
        Weak<Toplevel> grabbed_toplevel;
        vec2 grab;
        wlr_box grab_bounds;
        uint32_t resize_edges;
    } movesize;

    struct {
        Weak<Toplevel> current;
    } focus_cycle;

    uint32_t main_modifier;
    xkb_keysym_t main_modifier_keysym_left;
    xkb_keysym_t main_modifier_keysym_right;

    Weak<DragIcon> drag_icon;
    Weak<CursorSurface> cursor_surface;

    struct {
        Weak<Toplevel> toplevel;
        wlr_box selection;
        ColorRect selector;
        wlr_box initial_zone = {};
        wlr_box final_zone   = {};
        bool selecting = false;
    } zone;
};

struct MessageConnection
{
    Server* server;
    wl_event_source* source;
    std::filesystem::path cwd;
    int fd;
};

enum class MessageType : uint32_t
{
    Argument = 1,
    StdOut   = 2,
    StdErr   = 3,
};

struct MessageHeader
{
    MessageType type;
    uint32_t    size;
};

#define GET_WL_CLIENT_CMDLINE 0

struct Client
{
    Server* server;
    struct wl_client* wl_client;

    ListenerSet listeners;

    pid_t pid;
    uid_t uid;
    gid_t gid;

    std::filesystem::path path;
#if GET_WL_CLIENT_CMDLINE
    std::vector<std::string> cmdline;
#endif
    std::string process_name;

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
    vec2 accel_remainder = {};
    vec2 rel_accel_remainder = {};

    static Pointer* from(struct wlr_pointer* pointer) { return pointer ? static_cast<Pointer*>(pointer->data) : nullptr; }
};

struct Output
{
    static Output* from(struct wlr_output* output) { return output ? static_cast<Output*>(output->data) : nullptr; }

    ListenerSet listeners;

    Server* server;
    struct wlr_output* wlr_output;
    wlr_output_layout_output* layout_output() const;

    wlr_box workarea;

    EnumMap<std::vector<LayerSurface*>, zwlr_layer_shell_v1_layer> layers;
};

// -----------------------------------------------------------------------------

enum class SurfaceRole : uint32_t
{
    invalid,
    toplevel,
    popup,
    layer_surface,
    subsurface,
    drag_icon,
};

struct Surface : WeaklyReferenceable
{
    SurfaceRole role = SurfaceRole::invalid;

    ListenerSet listeners;

    Server* server;
    struct wlr_surface* wlr_surface;

    ivec2 cached_position;

    float last_scale = 0.f;

    std::vector<Popup*> popups;

    struct {
        bool surface_set;
        Weak<CursorSurface> surface;
    } cursor;

    static Surface* from_data(void* data)
    {
        Surface* surface = static_cast<Surface*>(data);
        return (surface && surface->role != SurfaceRole::invalid) ? surface : nullptr;
    }
    static Surface* from(struct wlr_surface* surface) { return surface ? Surface::from_data(surface->data) : nullptr; }
};

struct Subsurface : Surface
{
    static Subsurface* from(Surface* surface)
    {
        return (surface && surface->role == SurfaceRole::subsurface) ? static_cast<Subsurface*>(surface) : nullptr;
    }
    static Subsurface* from(struct wlr_surface* wlr_surface) { return from(Surface::from(wlr_surface)); }

    wlr_subsurface* subsurface() const { return wlr_subsurface_try_from_wlr_surface(wlr_surface); }
    Surface* parent() const { return Surface::from(subsurface()->parent); }
};

struct Toplevel : Surface
{
    static Toplevel* from(Surface* surface)
    {
        return (surface && surface->role == SurfaceRole::toplevel) ? static_cast<Toplevel*>(surface) : nullptr;
    }
    static Toplevel* from(struct wlr_surface* wlr_surface)  { return from(Surface::from(wlr_surface)); }
    static Toplevel* from(wlr_xdg_toplevel*   xdg_toplevel) { return xdg_toplevel ? Toplevel::from(xdg_toplevel->base->surface) : nullptr; }

    wlr_xdg_toplevel* xdg_toplevel() const { return wlr_xdg_toplevel_try_from_wlr_surface(wlr_surface); }

    struct {
        wlr_xdg_toplevel_decoration_v1* xdg_decoration;
        ListenerSet listeners;
    } decoration;

    wlr_box prev_bounds;

    ivec2     anchor;
    wlr_edges anchor_edges;

    struct {
        bool enable_throttle_resize = true;

        bool any_pending = false;
        int pending_width;
        int pending_height;

        uint32_t last_resize_serial = 0;
        uint32_t last_commited_serial = 0;
    } resize;

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

    wlr_xdg_popup* xdg_popup() const { return wlr_xdg_popup_try_from_wlr_surface(wlr_surface); }
};

struct LayerSurface : Surface
{
    static LayerSurface* from(Surface* surface)
    {
        return (surface && surface->role == SurfaceRole::layer_surface) ? static_cast<LayerSurface*>(surface) : nullptr;
    }
    static LayerSurface* from(struct wlr_surface* wlr_surface) { return from(Surface::from(wlr_surface)); }

    wlr_layer_surface_v1* wlr_layer_surface() const { return wlr_layer_surface_v1_try_from_wlr_surface(wlr_surface); }

    ivec2 position;
};

struct DragIcon : Surface
{

};

struct CursorSurface : Surface
{
    // This listener inherits from Surface with an `invalid` role so that
    // Surface::from(struct wlr_surface*) calls are still always safe to make

    ivec2 hotspot;
};

// ---- Server -----------------------------------------------------------------

void server_request_quit(Server*, bool force);

// ---- Watchdog ---------------------------------------------------------------

void watchdog_init(Server*);
void watchdog_start_shutdown();

// ---- Binds ------------------------------------------------------------------

Modifiers mod_from_string(std::string_view name);

std::optional<Bind> bind_from_string(Server*, std::string_view bind_string);
void                bind_erase(      Server*, Bind);
void                bind_register(   Server*, const CommandBind&);
bool                bind_trigger(    Server*, Bind);

// ---- IPC --------------------------------------------------------------------

void ipc_server_init(   Server*);
void ipc_server_cleanup(Server*);

int ipc_client_run(std::span<const std::string_view>);

void ipc_send_string(int fd, MessageType type, std::string_view str);

// ---- Process ----------------------------------------------------------------

void env_set(Server*, std::string_view name, std::optional<std::string_view> value);

struct SpawnEnvAction { const char* name; const char* value; };
void spawn(Server*, std::string_view file, std::span<const std::string_view> argv, std::span<const SpawnEnvAction> env_actions = {}, const char* wd = nullptr);

// ---- Script -----------------------------------------------------------------

void script_system_init(Server*);
void script_run(        Server*, std::string_view source, const std::filesystem::path& source_dir);
void script_run_file(   Server*, const std::filesystem::path& script_path);

// ---- Policy -----------------------------------------------------------------

void set_interaction_mode(Server*, InteractionMode);

void      focus_cycle_begin(Server*, wlr_cursor*);
void      focus_cycle_step( Server*, wlr_cursor*, bool backwards);
Toplevel* focus_cycle_end(  Server*);

bool input_handle_key(   Server*, const wlr_keyboard_key_event&, xkb_keysym_t sym);
bool input_handle_button(Server*, const wlr_pointer_button_event&);
bool input_handle_axis(  Server*, const wlr_pointer_axis_event&);

Modifiers get_modifiers(Server*);
bool      check_mods(   Server*, Modifiers);

// ---- Scene ------------------------------------------------------------------

void scene_output_frame(Output* output, timespec now);

// ---- Client -----------------------------------------------------------------

void client_new(    wl_listener*, void*);
void client_destroy(wl_listener*, void*);

bool client_filter_globals(const wl_client*, const wl_global*, void*);

// ---- Keyboard ---------------------------------------------------------------

void keyboard_new(Server*, wlr_input_device*);

void keyboard_handle_modifiers(wl_listener*, void*);
void keyboard_handle_key(      wl_listener*, void*);
void keyboard_handle_destroy(  wl_listener*, void*);

// ---- Pointer ----------------------------------------------------------------

bool is_cursor_visible(  Server*);
void update_cursor_state(Server*, bool from_commit = false);

void cursor_surface_commit( wl_listener*, void*);
void cursor_surface_destroy(wl_listener*, void*);

bool cursor_surface_is_visible(CursorSurface*);

vec2 get_cursor_pos(Server*);

uint32_t get_num_pointer_buttons_down(Server*);

void process_cursor_resize(Server*);
void process_cursor_motion(Server*, uint32_t time_msecs, wlr_input_device*, vec2 delta, vec2 rel, vec2 rel_unaccel);

void seat_request_set_cursor(  wl_listener*, void*);
void seat_pointer_focus_change(wl_listener*, void*);

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

void zone_process_cursor_motion(Server*);
bool zone_process_cursor_button(Server*, const wlr_pointer_button_event&);
void zone_end_selection(        Server*);

wlr_box zone_apply_external_padding(Server*, wlr_box);

// ---- Output -----------------------------------------------------------------

void output_manager_apply(wl_listener*, void*);
void output_manager_test( wl_listener*, void*);

Output* get_output_at(              Server*, vec2);
Output* get_nearest_output_to_point(Server*, vec2);
Output* get_nearest_output_to_box(  Server*, wlr_box);

Output* get_output_for_surface(Surface*);

wlr_box output_get_bounds( Output*);
void    output_reconfigure(Output*);

void output_frame(        wl_listener*, void*);
void output_request_state(wl_listener*, void*);
void output_destroy(      wl_listener*, void*);
void output_new(          wl_listener*, void*);
void output_layout_change(wl_listener*, void*);

// ---- Surface ----------------------------------------------------------------

void surface_update_scale(Surface*);

Surface* get_surface_accepting_input_at(Server*, vec2 layout_pos, wlr_surface** p_surface, vec2* surface_pos);
Surface* get_focused_surface(           Server*);

void surface_try_focus(Server*, Surface*);
void update_focus(     Server*);

wlr_box surface_get_bounds(      Surface*);
wlr_box surface_get_geometry(    Surface*);
wlr_box surface_get_coord_system(Surface*);

bool surface_is_mapped(    Surface* surface);
bool surface_accepts_focus(Surface* surface);

void surface_cleanup(Surface*);

void request_activate(wl_listener*, void*);

// ---- Surface.Subsurface -----------------------------------------------------

void subsurface_new(    wl_listener*, void*);
void subsurface_destroy(wl_listener*, void*);

// ---- Surface.LayerSurface ---------------------------------------------------

void output_reconfigure_layer(Output*, zwlr_layer_shell_v1_layer);

void layer_surface_commit( wl_listener*, void*);
void layer_surface_map(    wl_listener*, void*);
void layer_surface_unmap(  wl_listener*, void*);
void layer_surface_destroy(wl_listener*, void*);
void layer_surface_new(    wl_listener*, void*);

// ---- Surface.Toplevel -------------------------------------------------------

float toplevel_get_opacity(Toplevel*);

void toplevel_set_bounds(       Toplevel*, wlr_box, wlr_edges locked_edges = wlr_edges(WLR_EDGE_LEFT | WLR_EDGE_TOP));
void toplevel_set_activated(    Toplevel*, bool active);
bool toplevel_is_fullscreen(    Toplevel*);
void toplevel_set_fullscreen(   Toplevel*, bool fullscreen, Output* output);
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

std::string surface_to_string(           Surface*                  );
std::string pointer_constraint_to_string(wlr_pointer_constraint_v1*);
std::string client_to_string(            Client*                   );
std::string cursor_surface_to_string(    CursorSurface*            );
std::string pointer_to_string(           Pointer*                  );
std::string output_to_string(            Output*                   );
