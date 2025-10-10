#pragma once

// pre-includes for wlr_scene.h
#include <pixman.h>
#include <time.h>

// pre-includes for xwayland.h
#include <stdbool.h>
#include <wayland-server-core.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>

#ifdef __cplusplus
extern "C" {
#endif

// backends
#include <wlr/backend.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/wayland.h>
#include <wlr/backend/x11.h>
#include <wlr/backend/headless.h>

// render
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>

// utils
#include <wlr/util/edges.h>
#include <wlr/util/log.h>

// types
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#define static
# include <wlr/types/wlr_scene.h> // [static X] array parameters are not valid C++
#undef static
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>

// xwayland
#define class class_
# include <wlr/xwayland/xwayland.h> // uses reserved keyword "class" as struct member
#undef class
#include <wlr/xwayland.h>

#ifdef __cplusplus
}
#endif
