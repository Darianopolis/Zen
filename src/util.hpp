#pragma once

#include "pch.hpp"
#include "log.hpp"

// -----------------------------------------------------------------------------

inline
void log_unix_error(std::string_view message, i32 err = 0)
{
    err = err ?: errno;

    if (message.empty()) { log_error("({}) {}",              err, strerror(err)); }
    else                 { log_error("{}: ({}) {}", message, err, strerror(err)); }
}

enum class unix_error_behavior : u32
{
    ret_null,
    ret_neg1,
    ret_neg_errno,
    check_errno,
};

template<unix_error_behavior B>
struct unix_check_helper
{
    template<typename T>
    static constexpr
    T check(std::source_location loc, T res, auto... allowed)
    {
        bool error_occured = false;
        i32 error_code = 0;

        if constexpr (B == unix_error_behavior::ret_null)     if (!res)      { error_occured = true; error_code = errno; }
        if constexpr (B == unix_error_behavior::ret_neg1)     if (res == -1) { error_occured = true; error_code = errno; }
        if constexpr (B == unix_error_behavior::ret_neg_errno) if (res < 0)   { error_occured = true; error_code = -res;  }
        if constexpr (B == unix_error_behavior::check_errno)  if (errno)     { error_occured = true; error_code = errno; }

        if (!error_occured || (... || (error_code == allowed))) return res;

        log_unix_error(std::format("unix_check {}@{}", loc.file_name(), loc.line()), error_code);

        return res;
    }
};

#define unix_check_null(func, ...)                       unix_check_helper<unix_error_behavior::ret_null     >::check(std::source_location::current(), (func) __VA_OPT__(,) __VA_ARGS__)
#define unix_check_n1(func, ...)                         unix_check_helper<unix_error_behavior::ret_neg1     >::check(std::source_location::current(), (func) __VA_OPT__(,) __VA_ARGS__)
#define unix_check_ne(func, ...)                         unix_check_helper<unix_error_behavior::ret_neg_errno>::check(std::source_location::current(), (func) __VA_OPT__(,) __VA_ARGS__)
#define unix_check_ce(func, ...) [&] { errno = 0; return unix_check_helper<unix_error_behavior::check_errno  >::check(std::source_location::current(), (func) __VA_OPT__(,) __VA_ARGS__); }()


// -----------------------------------------------------------------------------

template<typename Fn>
struct Defer
{
    Fn fn;

    Defer(Fn&& fn): fn(std::move(fn)) {}
    ~Defer() { fn(); };
};

#define defer Defer _ = [&]

// -----------------------------------------------------------------------------

template<typename... Ts>
struct overload_set : Ts... {
    using Ts::operator()...;
};

template<typename... Ts>
overload_set(Ts...) -> overload_set<Ts...>;

// -----------------------------------------------------------------------------

constexpr
std::string ascii_to_upper(std::string_view in)
{
    std::string out(in);
    for (char& c : out) c = std::toupper(c);
    return out;
}

// -----------------------------------------------------------------------------

constexpr auto ptr(auto&& value) { return &value; }

// -----------------------------------------------------------------------------

#define FUNC_REF(func) [](void* d, auto... args) { return (*static_cast<decltype(func)*>(d))(std::forward<decltype(args)>(args)...); }, &func

// -----------------------------------------------------------------------------

#define DECORATE_FLAG_ENUM(EnumType) \
    inline constexpr EnumType operator| (EnumType  l, EnumType r) { return EnumType(std::to_underlying(l) | std::to_underlying(r));                  } \
    inline constexpr EnumType operator|=(EnumType& l, EnumType r) { return l = l | r;                                                                } \
    inline constexpr bool     operator>=(EnumType  l, EnumType r) { return std::to_underlying(r) == (std::to_underlying(l) & std::to_underlying(r)); } \
    inline constexpr EnumType operator& (EnumType  l, EnumType r) { return EnumType(std::to_underlying(l) & std::to_underlying(r));                  } \
    inline constexpr EnumType operator~ (EnumType  v)             { return EnumType(~std::to_underlying(v));                                         }

// -----------------------------------------------------------------------------

template<typename T, typename E>
struct EnumMap
{
    T _data[magic_enum::enum_count<E>()];

    static
    EnumMap make(std::initializer_list<const std::pair<E, T>> values)
    {
        EnumMap map;
        for (auto[k, v] : values) {
            map[k] = v;
        }
        return map;
    }

    static constexpr auto enum_values = magic_enum::enum_values<E>();

    constexpr       T& operator[](E value)       { return _data[magic_enum::enum_index(value).value()]; }
    constexpr const T& operator[](E value) const { return _data[magic_enum::enum_index(value).value()]; }
};

// -----------------------------------------------------------------------------

using fvec4 = glm:: vec4;
using  vec2 = glm::dvec2;
using ivec2 = glm::ivec2;

// -----------------------------------------------------------------------------

constexpr vec2 copysign(     vec2 v, vec2 s) { return vec2(std::copysign(v.x, s.x), std::copysign(v.y, s.y)); }
constexpr vec2 round_to_zero(vec2 v)         { return copysign(glm::floor(glm::abs(v)), v);                   }

// -----------------------------------------------------------------------------

constexpr const f32* color_to_wlroots(fvec4 v, fvec4&& temp = {})
{
    return glm::value_ptr(temp = {glm::fvec3{v} * v.w, v.w});
}

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
    static constexpr auto constrain_axis = [](i32 start, i32 length, i32& origin, i32& extent) {
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
    if (signal) wl_signal_add(signal, &l->listener);
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

    Listener* add(Listener* l)
    {
        l->next = first;
        first = l;
        return l;
    }

    template<typename T>
    Listener* listen(wl_signal* signal, T userdata, void(*notify_func)(wl_listener*, void*))
    {
        return add(::listen(signal, userdata, notify_func));
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

    constexpr bool operator==(const Weak&) const = default;

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

template<typename T>
void fixup_weak_vector(std::vector<Weak<T>>& vector)
{
    std::erase_if(vector, [](const Weak<T>& v) { return !v.get(); });
}

template<typename T>
auto iterate(std::span<T> view, bool reverse = false)
{
    struct Iterator
    {
        std::span<T> view;
        i64 cur, end, step;

        bool operator==(std::default_sentinel_t) const { return cur == end; }
        void operator++() { cur += step; }
        T&   operator*()  { return view[cur]; }
    };

    struct Iterable
    {
        std::span<T> view;
        bool backward;

        Iterator begin() {
            return backward
                ? Iterator { view, i64(view.size()) - 1, -1, -1 }
                : Iterator { view, 0, i64(view.size()), 1 };
        }

        std::default_sentinel_t end() { return {}; }
    };

    return Iterable{view, reverse};
}

// -----------------------------------------------------------------------------

struct CommandParser
{
    std::span<const std::string_view> args;
    u32 index;

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

    std::optional<i32> get_i32() { return get_from_chars<i32>(); }
    std::optional<i32> get_f64() { return get_from_chars<f64>(); }
};

// -----------------------------------------------------------------------------

std::string duration_to_string(std::chrono::duration<f64, std::nano> dur);

// -----------------------------------------------------------------------------

wlr_buffer* buffer_from_pixels_scaled(wlr_allocator* allocator, wlr_renderer* renderer, u32 upload_format, u32 stride, u32 width, u32 height, const void* data, u32 dst_width, u32 dst_height);
wlr_buffer* buffer_from_pixels(wlr_allocator*, wlr_renderer*, u32 format, u32 stride, u32 width, u32 height, const void* data);

// -----------------------------------------------------------------------------

wlr_fbox rect_fill_compute_source_box(ivec2 source_extent, ivec2 target_extent);
