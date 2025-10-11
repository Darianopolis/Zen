#pragma once

#include "quartz_wlroots.h"

#include <typeinfo>
#include <cstring>

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

    auto l = new qz_listener{};
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
        l->listener.notify = nullptr;
    } else {
        wlr_log(WLR_INFO, "QUARTZ_TRACE: unlisten called on non-activated listener");
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
    auto l = qz_listener_from(listener);
#if QZ_TYPE_CHECKED_LISTENERS
    if (&typeid(T) != l->typeinfo) {
        wlr_log(WLR_ERROR, "qz_listener_userdata type match, expected '%s' got '%s'", l->typeinfo->name(), typeid(T).name());
        exit(1);
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
            auto next = cur->next;
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