#pragma once

#include "pch.hpp"
#include "log.hpp"

// -----------------------------------------------------------------------------

template<typename Fn>
struct Defer
{
    Fn fn;

    Defer(Fn&& fn): fn(std::move(fn)) {}
    ~Defer() { fn(); };
};

// -----------------------------------------------------------------------------

constexpr auto ptr(auto&& value) { return &value; }

// -----------------------------------------------------------------------------

#define FUNC_REF(func) [](void* d, auto... args) { return (*static_cast<decltype(func)*>(d))(std::forward<decltype(args)>(args)...); }, &func

// -----------------------------------------------------------------------------

template<typename T, typename E>
struct EnumMap
{
    T _data[magic_enum::enum_count<E>()];

    static constexpr auto enum_values = magic_enum::enum_values<E>();

    constexpr       T& operator[](E value)       { return _data[magic_enum::enum_index(value).value()]; }
    constexpr const T& operator[](E value) const { return _data[magic_enum::enum_index(value).value()]; }
};

// -----------------------------------------------------------------------------

struct Color
{
    float values[4];

    constexpr Color() = default;

    constexpr Color(float r, float g, float b, float a)
        : values { r * a, g * a, b * a, a }
    {}
};

// -----------------------------------------------------------------------------

struct Point
{
    double x, y;
};

constexpr
double distance_between(Point a, Point b)
{
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

struct Box
{
    double x, y, width, height;
};

constexpr
wlr_box box_round_to_wlr_box(Box in)
{
    wlr_box out;
    out.x = std::floor(in.x);
    out.y = std::floor(in.y);
    out.width = std::ceil(in.x + in.width - out.x);
    out.height = std::ceil(in.y + in.height - out.y);
    return out;
};

constexpr
Box box_outer(Box a, Box b)
{
    double left   = std::min(a.x,            b.x);
    double top    = std::min(a.y,            b.y);
    double right  = std::max(a.x + a.width,  b.x + b.width);
    double bottom = std::max(a.y + a.height, b.y + b.height);
    return {
        .x = left,
        .y = top,
        .width = right - left,
        .height = bottom - top,
    };
};

constexpr
bool box_contains_point(Box box, Point p)
{
    double l = box.x;
    double t = box.y;
    double r = box.x + box.width;
    double b = box.y + box.height;
    return p.x >= l && p.x < r && p.y >= t && p.y < b;
};

// -----------------------------------------------------------------------------

constexpr int box_left(wlr_box box)   { return box.x; }
constexpr int box_top(wlr_box box)    { return box.y; }
constexpr int box_right(wlr_box box)  { return box.x + box.width;  }
constexpr int box_bottom(wlr_box box) { return box.y + box.height; }

constexpr
wlr_box constrain_box(wlr_box box, wlr_box bounds)
{
    if (box.width >= bounds.width) {
        box.x = bounds.x;
        box.width = bounds.width;
    } else {
        if (int overlap_x = box_right(box) - box_right(bounds);  overlap_x > 0) box.x -= overlap_x;
        box.x = std::max(box.x, bounds.x);
    }

    if (box.height >= bounds.height) {
        box.y = bounds.y;
        box.height = bounds.height;
    } else {
        if (int overlap_y = box_bottom(box) - box_bottom(bounds); overlap_y > 0) box.y -= overlap_y;
        box.y = std::max(box.y, bounds.y);
    }

    return box;
}

// -----------------------------------------------------------------------------

struct SpawnEnvAction { const char* name; const char* value; };
void spawn(const char* file, std::span<const std::string_view> argv, std::span<const SpawnEnvAction> env_actions = {});

// -----------------------------------------------------------------------------

#define TYPE_CHECKED_LISTENERS 1

struct Listener
{
    Listener* next;
    void* userdata;
    wl_listener listener;

#if TYPE_CHECKED_LISTENERS
    const std::type_info* typeinfo;
#endif
};

template<typename T>
Listener* listen(wl_signal* signal, T userdata, void(*notify_func)(wl_listener*, void*))
{
    static_assert(sizeof(userdata) <= sizeof(void*));

    Listener* l = new Listener{};
#if TYPE_CHECKED_LISTENERS
    l->typeinfo = &typeid(T);
#endif
    std::memcpy(&l->userdata, &userdata, sizeof(T));
    l->listener.notify = notify_func;
    wl_signal_add(signal, &l->listener);
    return l;
}

inline
void unlisten(Listener* l)
{
    if (l->listener.notify) {
        wl_list_remove(&l->listener.link);
    }
    delete l;
}

inline
Listener* listener_from(wl_listener* listener)
{
    Listener* l = wl_container_of(listener, l, listener);
    return l;
}

template<typename T>
T listener_userdata(wl_listener* listener)
{
    Listener* l = listener_from(listener);
#if TYPE_CHECKED_LISTENERS
    if (&typeid(T) != l->typeinfo) {
        log_error("listener_userdata type match, expected '{}' got '{}'", l->typeinfo->name(), typeid(T).name());
        return {};
    }
#endif
    T userdata;
    std::memcpy(&userdata, &l->userdata, sizeof(T));
    return userdata;
}

struct ListenerSet
{
    Listener* first = nullptr;

    ~ListenerSet() { clear(); }

    void clear()
    {
        Listener* cur = first;
        while (cur) {
            Listener* next = cur->next;
            unlisten(cur);
            cur = next;
        }
        first = nullptr;
    }

    void add(Listener* l)
    {
        l->next = first;
        first = l;
    }

    template<typename T>
    void listen(wl_signal* signal, T userdata, void(*notify_func)(wl_listener*, void*))
    {
        add(::listen(signal, userdata, notify_func));
    }
};

// -----------------------------------------------------------------------------

bool walk_scene_tree_back_to_front(wlr_scene_node* node, double sx, double sy, bool(*for_each)(void*, wlr_scene_node*, double, double), void* for_each_data, bool filter_disabled);
bool walk_scene_tree_front_to_back(wlr_scene_node* node, double sx, double sy, bool(*for_each)(void*, wlr_scene_node*, double, double), void* for_each_data, bool filter_disabled);
