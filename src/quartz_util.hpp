#pragma once

#include "quartz_wlroots.h"

#include <typeinfo>
#include <cstring>
#include <span>
#include <string_view>

// -----------------------------------------------------------------------------

struct qz_color
{
    float values[4];

    constexpr qz_color() = default;

    constexpr qz_color(float r, float g, float b, float a)
        : values { r * a, g * a, b * a, a }
    {}
};

struct qz_point
{
    double x, y;
};

struct qz_box
{
    double x, y, width, height;
};

constexpr
wlr_box qz_box_round_to_wlr_box(qz_box in)
{
    wlr_box out;
    out.x = std::floor(in.x);
    out.y = std::floor(in.y);
    out.width = std::ceil(in.x + in.width - out.x);
    out.height = std::ceil(in.y + in.height - out.y);
    return out;
};

constexpr
qz_box qz_box_outer(qz_box a, qz_box b)
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
bool qz_box_contains_point(qz_box box, qz_point p)
{
    auto l = box.x;
    auto t = box.y;
    auto r = box.x + box.width;
    auto b = box.y + box.height;
    return p.x >= l && p.x < r && p.y >= t && p.y < b;
};

// -----------------------------------------------------------------------------

void qz_spawn(const char* file, std::span<const std::string_view> argv);
constexpr auto qz_ptr(auto&& value) { return &value; }

// -----------------------------------------------------------------------------

#define QZ_TYPE_CHECKED_LISTENERS 1

struct qz_listener
{
    qz_listener* next;
    void* userdata;
    wl_listener listener;

#if QZ_TYPE_CHECKED_LISTENERS
    const std::type_info* typeinfo;
#endif
};

template<typename T>
qz_listener* qz_listen(wl_signal* signal, T userdata, void(*notify_func)(wl_listener*, void*))
{
    static_assert(sizeof(userdata) <= sizeof(void*));

    qz_listener* l = new qz_listener{};
#if QZ_TYPE_CHECKED_LISTENERS
    l->typeinfo = &typeid(T);
#endif
    std::memcpy(&l->userdata, &userdata, sizeof(T));
    l->listener.notify = notify_func;
    wl_signal_add(signal, &l->listener);
    return l;
}

inline
void qz_unlisten(qz_listener* l)
{
    if (l->listener.notify) {
        wl_list_remove(&l->listener.link);
    }
    delete l;
}

inline
qz_listener* qz_listener_from(wl_listener* listener)
{
    qz_listener* l = wl_container_of(listener, l, listener);
    return l;
}

template<typename T>
T qz_listener_userdata(wl_listener* listener)
{
    qz_listener* l = qz_listener_from(listener);
#if QZ_TYPE_CHECKED_LISTENERS
    if (&typeid(T) != l->typeinfo) {
        wlr_log(WLR_ERROR, "qz_listener_userdata type match, expected '%s' got '%s'", l->typeinfo->name(), typeid(T).name());
        return {};
    }
#endif
    T userdata;
    std::memcpy(&userdata, &l->userdata, sizeof(T));
    return userdata;
}

struct qz_listener_set
{
    qz_listener* first = nullptr;

    ~qz_listener_set() { clear(); }

    void clear()
    {
        qz_listener* cur = first;
        while (cur) {
            qz_listener* next = cur->next;
            qz_unlisten(cur);
            cur = next;
        }
        first = nullptr;
    }

    void add(qz_listener* l)
    {
        l->next = first;
        first = l;
    }

    template<typename T>
    void listen(wl_signal* signal, T* userdata, void(*notify_func)(wl_listener*, void*))
    {
        add(qz_listen(signal, userdata, notify_func));
    }
};
