#include "core.hpp"

struct SceneFrame
{
    Output* output;
    wlr_render_pass* renderpass;
    std::vector<Surface*> renderered;
};

static
void scene_render_color_rect(SceneFrame* frame, ColorRect rect)
{
    if (rect.box.width == 0 || rect.box.height == 0) return;

    auto layer_output = frame->output->layout_output();
    auto dst_box = rect.box;
    dst_box.x -= layer_output->x;
    dst_box.y -= layer_output->y;
    wlr_render_pass_add_rect(frame->renderpass, ptr(wlr_render_rect_options {
        .box = dst_box,
        .color = color_to_premult_wlr(rect.color),
        .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
    }));
}

static
void scene_update_under_cursor(Surface* surface)
{
    auto* server = surface->server;

    auto* ir = &surface->wlr_surface->input_region;

    auto sx = server->cursor->x - surface->cached_position.x;
    auto sy = server->cursor->y - surface->cached_position.y;

    pixman_box32_t box;
    bool contained = pixman_region32_contains_point(ir, sx, sy, &box);

    if (contained) {
        server->surface_under_cursor = weak_from(surface);
        if (Toplevel* toplevel = Toplevel::from(surface)) {
            server->toplevel_under_cursor = weak_from(toplevel);
        }
    }
}

static
void scene_render_subsurfaces(SceneFrame* frame, wl_list* subsurfaces, float opacity, ivec2 parent_pos);

static
void scene_render_surface(SceneFrame* frame, Surface* surface, float opacity, ivec2 position)
{
    auto* wlr_surface = surface->wlr_surface;
    auto x = position.x;
    auto y = position.y;

    if (!wlr_surface->buffer) return;
    if (!wlr_surface->buffer->texture) return;

    surface->cached_position = {x, y};

    scene_update_under_cursor(surface);

    // log_trace("rendering subsurface @ ({}, {})", x, y);

    {
        auto layer_output = frame->output->layout_output();

        ivec2 buffer_pos = position;
        buffer_pos += ivec2(wlr_surface->current.dx, wlr_surface->current.dy);
        wlr_box dst_box = { buffer_pos.x, buffer_pos.y, surface->wlr_surface->current.buffer_width, surface->wlr_surface->current.buffer_height };

        if (wlr_output_layout_intersects(surface->server->output_layout, frame->output->wlr_output, &dst_box)) {
            wlr_render_pass_add_texture(frame->renderpass, ptr(wlr_render_texture_options {
                .texture = wlr_surface->buffer->texture,
                .dst_box = { buffer_pos.x - layer_output->x, buffer_pos.y - layer_output->y },
                .alpha = opacity < 1.f ? &opacity : nullptr,
                .filter_mode = WLR_SCALE_FILTER_NEAREST,
                .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
            }));
            wlr_surface_send_enter(surface->wlr_surface, frame->output->wlr_output);
            wlr_presentation_surface_textured_on_output(surface->wlr_surface, frame->output->wlr_output);

            // TODO: We should only send frame callbacks for the "primary" monitor of a surface
            //       That is - the output that contains the largest portion of the surface
            frame->renderered.emplace_back(surface);
        } else {
            wlr_surface_send_leave(surface->wlr_surface, frame->output->wlr_output);
        }
    }

    scene_render_subsurfaces(frame, &surface->wlr_surface->current.subsurfaces_above, opacity, {x, y});
}

static
void scene_render_popups(SceneFrame* frame, Surface* surface, float opacity, ivec2 position);

static
void scene_render_popup(SceneFrame* frame, Popup* popup, float opacity, ivec2 parent_position)
{
    auto xdg_geom = popup->xdg_popup()->current.geometry;
    auto pos = parent_position + ivec2 { xdg_geom.x, xdg_geom.y };

    auto geom = popup->xdg_popup()->base->current.geometry;
    scene_render_surface(frame, popup, opacity, pos - ivec2{geom.x, geom.y});

    scene_render_popups(frame, popup, opacity, pos);
}

static
void scene_render_popups(SceneFrame* frame, Surface* surface, float opacity, ivec2 position)
{
    for (auto* popup : surface->popups) {
        scene_render_popup(frame, popup, opacity, position);
    }
}

static
void scene_render_subsurface(SceneFrame* frame, Subsurface* subsurface, float opacity, ivec2 parent_pos)
{
    auto x = parent_pos.x + subsurface->subsurface()->current.x;
    auto y = parent_pos.y + subsurface->subsurface()->current.y;

    scene_render_surface(frame, subsurface, opacity, {x, y});
}

static
void scene_render_subsurfaces(SceneFrame* frame, wl_list* subsurfaces, float opacity, ivec2 parent_pos)
{
    struct wlr_subsurface* wlr_subsurface;
    wl_list_for_each(wlr_subsurface, subsurfaces, current.link) {
        if (auto* subsurface = Subsurface::from(wlr_subsurface->surface)) {
            scene_render_subsurface(frame, subsurface, opacity, parent_pos);
        }
    }
}

static
void scene_render_layer_surface(SceneFrame* frame, LayerSurface* layer_surface)
{
    scene_render_surface(frame, layer_surface, 1.f, layer_surface->position);
    scene_render_popups(frame, layer_surface, 1.f, layer_surface->position);
}

static
void scene_render_layer_surfaces(SceneFrame* frame, Server* server, zwlr_layer_shell_v1_layer layer)
{
    for (LayerSurface* layer_surface : server->layer_surfaces) {
        if (layer_surface->wlr_layer_surface()->current.layer == layer) {
            scene_render_layer_surface(frame, layer_surface);
        }
    }
}

static
void scene_render_drag_icon(SceneFrame* frame, Server* server)
{
    DragIcon* drag_icon = server->drag_icon.get();
    if (!drag_icon) return;

    // auto geom = surface_get_geometry(drag_icon);
    auto pos = ivec2(get_cursor_pos(server));
        // - ivec2(geom.x - geom.width / 2, geom.y - geom.height / 2);

    scene_render_surface(frame, drag_icon, 1.f, pos);
}

static
void scene_render_toplevel(SceneFrame* frame, Toplevel* toplevel)
{
    auto* server = toplevel->server;

    struct wlr_surface* wlr_surface = toplevel->wlr_surface;
    if (!wlr_surface->buffer) return;

    auto& c = server->config.layout;

    if (!wlr_surface->buffer->texture) {
        log_error("Toplevel buffer does not have texture!");
        return;
    }

    wlr_box geom = surface_get_geometry(toplevel);

    if (!geom.width || !geom.height) return;

    wlr_box bounds = surface_get_bounds(toplevel);
    int gx = bounds.x;
    int gy = bounds.y;

    int x = gx - geom.x;
    int y = gy - geom.y;

    auto opacity = toplevel_get_opacity(toplevel);

    // Surface texture

    scene_render_surface(frame, toplevel, opacity, {x, y});

    bool show_borders = true;
    if (toplevel_is_fullscreen(toplevel)) show_borders = false;

    if (show_borders) {
        // Borders

        fvec4 border_color = get_focused_surface(toplevel->server) == toplevel ? c.border_color_focused : c.border_color_unfocused;
        border_color.a *= opacity;

        int border_width = c.border_width;

        static constexpr uint32_t left   = 0;
        static constexpr uint32_t top    = 1;
        static constexpr uint32_t right  = 2;
        static constexpr uint32_t bottom = 3;

        wlr_box positions[4];
        positions[left]   = { gx - border_width, gy - border_width, border_width, geom.height + border_width * 2 };
        positions[right]  = { gx + geom.width,   gy - border_width, border_width, geom.height + border_width * 2 };
        positions[top]    = { gx,                gy - border_width, geom.width,   border_width                   };
        positions[bottom] = { gx,                gy + geom.height,  geom.width,   border_width                   };

        for (uint32_t i = 0; i < 4; ++i) {
            scene_render_color_rect(frame, ColorRect {
                .box = positions[i],
                .color = border_color,
            });
        }
    }

    // Popups

    scene_render_popups(frame, toplevel, opacity, {gx, gy});
}

static
void scene_list_toplevels(Server* server, std::vector<Toplevel*>& lower_toplevels, Toplevel*& topmost)
{
    if (Toplevel* focus_cycle_toplevel = server->focus_cycle.current.get()) {
        for (auto& toplevel : server->toplevels) {
            if (toplevel != focus_cycle_toplevel) {
                lower_toplevels.emplace_back(toplevel);
            }
        }
        topmost = focus_cycle_toplevel;
    } else {
        lower_toplevels.append_range(server->toplevels);
        if (!lower_toplevels.empty()) {
            topmost = lower_toplevels.back();
            lower_toplevels.pop_back();
        }
    }
}

void scene_output_frame(Output* output, timespec now)
{
    Server* server = output->server;

    server->toplevel_under_cursor.reset();
    server->surface_under_cursor.reset();

    // log_debug("Output frame [{}]", now.tv_sec * 1000 + now.tv_nsec / 1000'000);

    wlr_output_state output_state;
    wlr_output_state_init(&output_state);
    auto* renderpass = wlr_output_begin_render_pass(output->wlr_output, &output_state, nullptr);
    if (!renderpass) {
        log_error("Failed to create renderpass, skipping frame...");
        return;
    }

    auto& c = server->config.layout;

    SceneFrame frame {
        .output = output,
        .renderpass = renderpass,
    };

    // Background clear

    wlr_render_pass_add_rect(renderpass, ptr(wlr_render_rect_options {
        .box = { 0, 0, output->wlr_output->width, output->wlr_output->height },
        .color = color_to_premult_wlr(c.background_color),
        .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
    }));

    // Layer surfaces below

    scene_render_layer_surfaces(&frame, server, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND);

    // Toplevels

    // TODO: Ensure parent/child windows are layered next to each other

    std::vector<Toplevel*> lower_toplevels;
    Toplevel* topmost = nullptr;
    scene_list_toplevels(server, lower_toplevels, topmost);

    // Lower toplevels

    for (Toplevel* toplevel : lower_toplevels) {
        scene_render_toplevel(&frame, toplevel);
    }

    // Layer surfaces between

    scene_render_layer_surfaces(&frame, server, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM);

    // Topmost toplevel

    if (topmost) {
        scene_render_toplevel(&frame, topmost);
    }

    // Layer surfaces above

    scene_render_layer_surfaces(&frame, server, ZWLR_LAYER_SHELL_V1_LAYER_TOP);
    scene_render_layer_surfaces(&frame, server, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY);

    // Zone selection rectangle

    scene_render_color_rect(&frame, server->zone.selector);

    // Drag Icon

    scene_render_drag_icon(&frame, server);

    // Cursor debug visual

    if (server->pointer.debug_visual_enabled) {
        // if (auto* cursor_surface = server->cursor_surface.get()) {
        //     scene_render_surface(&frame, cursor_surface, 1.f, ivec2(get_cursor_pos(server)) - cursor_surface->hotspot);
        // }

        int he = server->pointer.debug_visual_half_extent;
        scene_render_color_rect(&frame, ColorRect{
            .box = wlr_box { int(server->cursor->x - he), int(server->cursor->y - he), he * 2, he * 2 },
            .color = server->pointer.debug_visual_color,
        });
    }

    // Submit

    wlr_render_pass_submit(renderpass);
    wlr_output_commit_state(output->wlr_output, &output_state);
    wlr_output_state_finish(&output_state);

    // Send frame done to rendered surfaces

    for (Surface* surface : frame.renderered) {
        wlr_surface_send_frame_done(surface->wlr_surface, &now);
    }

    if (auto* cursor_surface = server->cursor_surface.get()) {
        wlr_surface_send_frame_done(cursor_surface->wlr_surface, &now);
    }
}
