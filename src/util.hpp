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

using fvec4 = glm:: vec4;
using  vec2 = glm::dvec2;
using ivec2 = glm::ivec2;

// -----------------------------------------------------------------------------

constexpr fvec4 premultiply(fvec4 v) { return {glm::fvec3{v} * v.w, v.w}; }

// -----------------------------------------------------------------------------

constexpr ivec2 box_origin(  const wlr_box& b) { return {b.x, b.y}; }
constexpr ivec2 box_extent(  const wlr_box& b) { return {b.width, b.height}; }
constexpr ivec2 box_opposite(const wlr_box& b) { return box_origin(b) + box_extent(b); };

constexpr
wlr_box box_outer(wlr_box a, wlr_box b)
{
    ivec2 origin = glm::min(box_origin(  a), box_origin(  b));
    ivec2 extent = glm::max(box_opposite(a), box_opposite(b)) - origin;
    return {origin.x, origin.y, extent.x, extent.y};
};

constexpr
wlr_box constrain_box(wlr_box box, wlr_box bounds)
{
    static constexpr auto constrain_axis = [](int start, int length, int& origin, int& extent) {
        if (extent > length) {
            origin = start;
            extent = length;
        } else {
            origin = std::max(origin, start) - std::max(0, (origin + extent) - (start + length));
        }
    };
    constrain_axis(bounds.x, bounds.width,  box.x, box.width);
    constrain_axis(bounds.y, bounds.height, box.y, box.height);
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

bool walk_scene_tree_back_to_front(wlr_scene_node* node, ivec2 node_pos, bool(*for_each)(void*, wlr_scene_node*, ivec2), void* for_each_data, bool filter_disabled);
bool walk_scene_tree_front_to_back(wlr_scene_node* node, ivec2 node_pos, bool(*for_each)(void*, wlr_scene_node*, ivec2), void* for_each_data, bool filter_disabled);

// -----------------------------------------------------------------------------

vec2 constrain_to_region(const pixman_region32_t* region, vec2 p1, vec2 p2, bool* was_inside);

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

    template<typename T>
    std::optional<T> get_from_chars()
    {
        if (index >= args.size()) return std::nullopt;

        T value;
        auto res = std::from_chars(args[index].begin(), args[index].end(), value);
        if (!res) return std::nullopt;

        index++;

        return value;
    }

    std::optional<int> get_int()    { return get_from_chars<int>(); }
    std::optional<int> get_double() { return get_from_chars<double>(); }
};
