#pragma once

#include "wlroots.hpp"
#include "log.hpp"

#include <typeinfo>
#include <cstring>
#include <span>
#include <string_view>

// -----------------------------------------------------------------------------

#define CONCAT_(a, b) a##b
#define CONCAT(a, b) CONCAT_(a, b)
#define UNIQUE_IDENT() CONCAT(jade_non_, __COUNTER__)

// -----------------------------------------------------------------------------

template<typename Fn>
struct Defer
{
    Fn fn;

    Defer(Fn&& fn): fn(std::move(fn)) {}
    ~Defer() { fn(); };
};

#define defer Defer UNIQUE_IDENT() = [&]

// -----------------------------------------------------------------------------

struct Color
{
    float values[4];

    constexpr Color() = default;

    constexpr Color(float r, float g, float b, float a)
        : values { r * a, g * a, b * a, a }
    {}
};

struct Point
{
    double x, y;
};

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
    auto left   = std::min(a.x,            b.x);
    auto top    = std::min(a.y,            b.y);
    auto right  = std::max(a.x + a.width,  b.x + b.width);
    auto bottom = std::max(a.y + a.height, b.y + b.height);
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
    auto l = box.x;
    auto t = box.y;
    auto r = box.x + box.width;
    auto b = box.y + box.height;
    return p.x >= l && p.x < r && p.y >= t && p.y < b;
};

// -----------------------------------------------------------------------------

void spawn(const char* file, std::span<const std::string_view> argv);
constexpr auto ptr(auto&& value) { return &value; }

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
    void listen(wl_signal* signal, T* userdata, void(*notify_func)(wl_listener*, void*))
    {
        add(::listen(signal, userdata, notify_func));
    }
};

// -----------------------------------------------------------------------------

template<typename Fn>
bool walk_scene_tree_reverse_depth_first(wlr_scene_node* node, double sx, double sy, Fn&& for_each)
{
    if (!node->enabled) return true;
    if (node->type == WLR_SCENE_NODE_TREE) {
        wlr_scene_tree* tree = wlr_scene_tree_from_node(node);
        wlr_scene_node* child;
        wl_list_for_each_reverse(child, &tree->children, link) {
            double nx = sx + child->x;
            double ny = sy + child->y;
            if (!walk_scene_tree_reverse_depth_first(child, nx, ny, std::forward<Fn>(for_each))) {
                return false;
            }
        }
    } else {
        if (!for_each(node, sx, sy)) return false;
    }

    return true;
}
