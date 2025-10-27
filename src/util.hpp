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
    constexpr Color(float r, float g, float b, float a): values{ r * a, g * a, b * a, a } {}
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

// -----------------------------------------------------------------------------

constexpr
wlr_box box_outer(wlr_box a, wlr_box b)
{
    int left   = std::min(a.x,            b.x);
    int top    = std::min(a.y,            b.y);
    int right  = std::max(a.x + a.width,  b.x + b.width);
    int bottom = std::max(a.y + a.height, b.y + b.height);
    return { left, top, right - left, bottom - top };
};

constexpr
wlr_box constrain_box(wlr_box box, wlr_box bounds)
{
    if (box.width >= bounds.width) {
        box.x = bounds.x;
        box.width = bounds.width;
    } else {
        if (int overlap_x = (box.x + box.width) - (bounds.x + bounds.width);  overlap_x > 0) box.x -= overlap_x;
        box.x = std::max(box.x, bounds.x);
    }

    if (box.height >= bounds.height) {
        box.y = bounds.y;
        box.height = bounds.height;
    } else {
        if (int overlap_y = (box.y + box.height) - (bounds.y + bounds.height); overlap_y > 0) box.y -= overlap_y;
        box.y = std::max(box.y, bounds.y);
    }

    return box;
}

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

// -----------------------------------------------------------------------------

Point constrain_to_region(const pixman_region32_t* region, Point p1, Point p2, bool* was_inside);

// -----------------------------------------------------------------------------

struct WeakState
{
    void* value;
};

struct WeaklyReferenceable
{
    std::shared_ptr<WeakState> weak_state;

    ~WeaklyReferenceable() { if (weak_state) weak_state->value = nullptr; }
};

template<typename T>
struct Weak
{
    std::shared_ptr<WeakState> weak_state;

    T*     get() { return weak_state ? static_cast<T*>(weak_state->value) : nullptr; }
    void reset() { weak_state = {}; }

    template<typename T2>
        requires std::derived_from<std::remove_cvref_t<T>, std::remove_cvref_t<T2>>
    operator Weak<T2>() { return Weak<T2>{weak_state}; }
};

template<typename T>
Weak<T> weak_from(T* t)
{
    if (!t) return {};
    if (!t->weak_state) t->weak_state.reset(new WeakState{t});
    return Weak<T>{t->weak_state};
}

// -----------------------------------------------------------------------------

struct CommandParser
{
    std::span<const std::string_view> args;
    uint32_t index;

    operator bool() const { return index < args.size(); }

    bool match(std::string_view arg)
    {
        if (index < args.size() && args[index] == arg) {
            index++;
            return true;
        }
        return false;
    }

    std::span<const std::string_view> peek_rest() { return args.subspan(index); }

    std::string_view peek()       { return index < args.size() ? args[index]   : std::string_view{}; }
    std::string_view get_string() { return index < args.size() ? args[index++] : std::string_view{}; }

    std::optional<int> get_int()
    {
        if (index >= args.size()) return std::nullopt;

        int value;
        auto res = std::from_chars(args[index].begin(), args[index].end(), value);
        if (!res) return std::nullopt;

        return value;
    }
};
