#include "core.hpp"

using RenderEntryOptions = std::variant<wlr_render_rect_options, wlr_render_texture_options>;

struct TransparentEntry
{
    pixman_region32 clip;
    float opacity = 1.f;
    RenderEntryOptions options;
};

struct SceneFrame
{
    Output* output;
    bool contains_cursor;
    wlr_render_pass* renderpass;
    pixman_region32 clip_region;
    std::vector<Surface*> renderered;

    pixman_region32 scratch_clipped_opaque;
    pixman_region32 scratch_clipped_opaque_transparent;

    std::vector<TransparentEntry> transparent_entries;
};

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
        if (!server->surface_under_cursor.get()) {
            server->surface_under_cursor = weak_from(surface);
        }
        if (Toplevel* toplevel = Toplevel::from(surface); toplevel && !server->toplevel_under_cursor.get()) {
            server->toplevel_under_cursor = weak_from(toplevel);
        }
    }
}

static
void scene_compute_clip_regions(SceneFrame* frame, wlr_box box, const pixman_region32* opaque_region)
{
    auto* clipped_opaque = &frame->scratch_clipped_opaque;
    auto* clipped_transparent = &frame->scratch_clipped_opaque_transparent;

    pixman_region32_clear(clipped_opaque);
    if (opaque_region) {
        pixman_region32_intersect(clipped_opaque, &frame->clip_region, opaque_region);
    }

    pixman_region32_clear(clipped_transparent);
    pixman_region32_union_rect(clipped_transparent, clipped_transparent, box.x, box.y, box.width, box.height);
    if (opaque_region) {
        pixman_region32_subtract(clipped_transparent, clipped_transparent, opaque_region);
    }
    pixman_region32_intersect(clipped_transparent, clipped_transparent, &frame->clip_region);
}

static
void scene_clip_opaque_region(SceneFrame* frame, const pixman_region32* clipped_opaque)
{
    pixman_region32_union(&frame->clip_region, &frame->clip_region, clipped_opaque);
    pixman_region32_subtract(&frame->clip_region, &frame->clip_region, clipped_opaque);
}

void scene_render_transparent_entry(SceneFrame* frame, const TransparentEntry& entry, wlr_render_rect_options options)
{
    options.clip = &entry.clip;
    options.blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED;
    wlr_render_pass_add_rect(frame->renderpass, &options);
}

void scene_render_transparent_entry(SceneFrame* frame, const TransparentEntry& entry, wlr_render_texture_options options)
{
    options.clip = &entry.clip;
    options.alpha = &entry.opacity;
    options.blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED;
    wlr_render_pass_add_texture(frame->renderpass, &options);
}

static
void scene_render_transparent_entries(SceneFrame* frame)
{
    for (auto& entry : iterate<TransparentEntry>(frame->transparent_entries, true)) {
        std::visit([&](auto& options) {
            scene_render_transparent_entry(frame, entry, options);
        }, entry.options);
    }
}

static
void scene_render_color_rect(SceneFrame* frame, ColorRect rect)
{
    if (rect.box.width == 0 || rect.box.height == 0) return;

    auto layer_output = frame->output->layout_output();
    auto dst_box = rect.box;
    dst_box.x -= layer_output->x;
    dst_box.y -= layer_output->y;

    pixman_region32 opaque_region = {};
    if (rect.color.a == 1.f) {
        pixman_region32_init_rect(&opaque_region, dst_box.x, dst_box.y, dst_box.width, dst_box.height);
    } else {
        pixman_region32_init(&opaque_region);
    }
    defer {
        pixman_region32_fini(&opaque_region);
    };

    scene_compute_clip_regions(frame, dst_box, &opaque_region);

    wlr_render_rect_options options {
        .box = dst_box,
        .color = color_to_premult_wlr(rect.color),
        .clip = &frame->scratch_clipped_opaque,
        .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
    };

    if (!pixman_region32_empty(&frame->scratch_clipped_opaque)) {
        wlr_render_pass_add_rect(frame->renderpass, &options);
    }

    if (!pixman_region32_empty(&frame->scratch_clipped_opaque_transparent))
    {
        auto& entry = frame->transparent_entries.emplace_back(TransparentEntry{
            .options = options,
        });
        pixman_region32_init(&entry.clip);
        pixman_region32_copy(&entry.clip, &frame->scratch_clipped_opaque_transparent);
    }

    scene_clip_opaque_region(frame, &opaque_region);
}

static
void scene_render_subsurfaces(SceneFrame* frame, wl_list* subsurfaces, float opacity, ivec2 parent_pos);

static
void scene_render_surface(SceneFrame* frame, Surface* surface, float opacity, ivec2 position, bool force_opaque = false)
{
    auto* wlr_surface = surface->wlr_surface;
    auto x = position.x;
    auto y = position.y;

    if (!wlr_surface->buffer) return;
    if (!wlr_surface->buffer->texture) return;

    surface->cached_position = {x, y};

    scene_render_subsurfaces(frame, &surface->wlr_surface->current.subsurfaces_above, opacity, {x, y});

    // log_trace("rendering subsurface @ ({}, {})", x, y);

    {
        auto layer_output = frame->output->layout_output();

        ivec2 buffer_pos = position;
        buffer_pos += ivec2(wlr_surface->current.dx, wlr_surface->current.dy);
        wlr_box dst_box = { buffer_pos.x, buffer_pos.y, surface->wlr_surface->current.buffer_width, surface->wlr_surface->current.buffer_height };

        if (wlr_output_layout_intersects(surface->server->output_layout, frame->output->wlr_output, &dst_box)) {
            dst_box.x -= layer_output->x;
            dst_box.y -= layer_output->y;

            pixman_region32 opaque_region;
            pixman_region32_init(&opaque_region);
            defer {
                pixman_region32_fini(&opaque_region);
            };
            if (opacity == 1.f) {
                if (force_opaque) {
                    pixman_region32_init_rect(&opaque_region, dst_box.x, dst_box.y, dst_box.width, dst_box.height);
                } else {
                    pixman_region32_copy(&opaque_region, &wlr_surface->opaque_region);
                    pixman_region32_translate(&opaque_region, dst_box.x, dst_box.y);
                }
            }

            scene_compute_clip_regions(frame, dst_box, &opaque_region);

            wlr_render_texture_options options {
                .texture = wlr_surface->buffer->texture,
                .dst_box = dst_box,
                .clip = &frame->clip_region,
                .filter_mode = WLR_SCALE_FILTER_NEAREST,
                .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
            };

            {
                auto cursor_pos = get_cursor_pos(frame->output->server);
                pixman_box32 box;
                if (pixman_region32_contains_point(&frame->scratch_clipped_opaque, cursor_pos.x, cursor_pos.y, &box)
                        || pixman_region32_contains_point(&frame->scratch_clipped_opaque_transparent, cursor_pos.x, cursor_pos.y, &box)) {
                    scene_update_under_cursor(surface);
                }
            }

            if (!pixman_region32_empty(&frame->scratch_clipped_opaque)) {
                wlr_render_pass_add_texture(frame->renderpass, &options);
            }

            if (!pixman_region32_empty(&frame->scratch_clipped_opaque_transparent))
            {
                auto& entry = frame->transparent_entries.emplace_back(TransparentEntry{
                    .opacity = opacity,
                    .options = options,
                });
                pixman_region32_init(&entry.clip);
                pixman_region32_copy(&entry.clip, &frame->scratch_clipped_opaque_transparent);
            }

            scene_clip_opaque_region(frame, &opaque_region);

            wlr_surface_send_enter(surface->wlr_surface, frame->output->wlr_output);
            wlr_presentation_surface_textured_on_output(surface->wlr_surface, frame->output->wlr_output);

            if (!pixman_region32_empty(&frame->scratch_clipped_opaque) || !pixman_region32_empty(&frame->scratch_clipped_opaque_transparent)) {
                if (get_output_for_surface(surface) == frame->output) {
                    // TODO: In the case that the surface is completely occluded on its primary output but visible elsewhere,
                    //       we should instead send frame done events for that output.
                    frame->renderered.emplace_back(surface);
                }
            }
        } else {
            wlr_surface_send_leave(surface->wlr_surface, frame->output->wlr_output);
        }
    }

    scene_render_subsurfaces(frame, &surface->wlr_surface->current.subsurfaces_below, opacity, {x, y});
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
    wl_list_for_each_reverse(wlr_subsurface, subsurfaces, current.link) {
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
void scene_render_layer_surfaces(SceneFrame* frame, zwlr_layer_shell_v1_layer layer)
{
    for (LayerSurface* layer_surface : frame->output->server->layer_surfaces) {
        if (layer_surface->wlr_layer_surface()->current.layer == layer) {
            scene_render_layer_surface(frame, layer_surface);
        }
    }
}

static
void scene_render_drag_icon(SceneFrame* frame)
{
    Server* server = frame->output->server;

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

    bool fullscreen = toplevel_is_fullscreen(toplevel);

    if (fullscreen) {
        if (get_output_for_surface(toplevel) != frame->output) {
            return;
        }
    }

    // Popups

    scene_render_popups(frame, toplevel, opacity, {gx, gy});

    // Borders

    bool show_borders = true;
    if (fullscreen) show_borders = false;

    if (show_borders) {
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

    // Surface texture

    bool force_opaque = fullscreen;
    scene_render_surface(frame, toplevel, opacity, {x, y}, force_opaque);

    // Fullscreen backing rectangle

    if (fullscreen) {
        auto* o = frame->output->wlr_output;
        auto* lo = frame->output->layout_output();
        scene_render_color_rect(frame, ColorRect {
            .box = { lo->x, lo->y, o->width, o->height },
            .color = {0, 0, 0, 1},
        });
    }
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

    bool contains_cursor = wlr_output_layout_contains_point(server->output_layout, output->wlr_output, server->cursor->x, server->cursor->y);
    if (contains_cursor) {
        log_debug("scene output {} contains cursor", output->wlr_output->name);
        server->toplevel_under_cursor.reset();
        server->surface_under_cursor.reset();
    }

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
        .contains_cursor = contains_cursor,
        .renderpass = renderpass,
    };

    pixman_region32_init_rect(&frame.clip_region, 0, 0, output->wlr_output->width, output->wlr_output->height);
    pixman_region32_init(&frame.scratch_clipped_opaque);
    pixman_region32_init(&frame.scratch_clipped_opaque_transparent);
    defer {
        pixman_region32_fini(&frame.clip_region);
        pixman_region32_fini(&frame.scratch_clipped_opaque);
        pixman_region32_fini(&frame.scratch_clipped_opaque_transparent);
    };

    // TODO: Ensure parent/child windows are layered next to each other
    std::vector<Toplevel*> lower_toplevels;
    Toplevel* topmost = nullptr;
    scene_list_toplevels(server, lower_toplevels, topmost);

    // NOTE: Items here are listed FRONT-TO-BACK, as we first render opaque regions clipped front-to-back,
    //       then unobstructed transparent regions back-to-front

    // Cursor debug visual

    if (server->pointer.debug_visual_enabled) {
        int offset = -server->pointer.debug_visual_half_extent * 2;
        int extent =  server->pointer.debug_visual_half_extent * 2;
        scene_render_color_rect(&frame, ColorRect{
            .box = wlr_box { int(server->cursor->x + offset), int(server->cursor->y + offset), extent, extent },
            .color = server->pointer.debug_visual_color,
        });
    }

    // Drag Icon

    scene_render_drag_icon(&frame);

    // Zone selection rectangle

    scene_render_color_rect(&frame, server->zone.selector);

    // Layer surfaces above

    scene_render_layer_surfaces(&frame, ZWLR_LAYER_SHELL_V1_LAYER_TOP);
    scene_render_layer_surfaces(&frame, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY);

    // Topmost toplevel

    if (topmost) {
        scene_render_toplevel(&frame, topmost);
    }

    // Layer surfaces between

    scene_render_layer_surfaces(&frame, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM);

    // Lower toplevels

    for (Toplevel* toplevel : iterate<Toplevel*>(lower_toplevels, true)) {
        scene_render_toplevel(&frame, toplevel);
    }

    // Layer surfaces below

    scene_render_layer_surfaces(&frame, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND);

    // Background clear

    auto* lo = output->layout_output();
    scene_render_color_rect(&frame, ColorRect {
        .box = { lo->x, lo->y, output->wlr_output->width, output->wlr_output->height },
        .color = c.background_color,
    });

    // Render transparent entries back-to-front

    scene_render_transparent_entries(&frame);

    // ---- Submit ----

    wlr_render_pass_submit(renderpass);
    wlr_output_commit_state(output->wlr_output, &output_state);
    wlr_output_state_finish(&output_state);

    // Send frame done to rendered surfaces

    for (Surface* surface : frame.renderered) {
        wlr_surface_send_frame_done(surface->wlr_surface, &now);
    }
}
