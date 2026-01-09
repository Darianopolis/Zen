#include "pch.hpp"
#include "core.hpp"

// -----------------------------------------------------------------------------

bool walk_scene_tree_back_to_front(wlr_scene_node* node, ivec2 node_pos, bool(*for_each)(void*, wlr_scene_node*, ivec2), void* for_each_data, bool filter_disabled)
{
    if (filter_disabled && !node->enabled) return true;
    if (!for_each(for_each_data, node, node_pos)) return false;

    if (node->type == WLR_SCENE_NODE_TREE) {
        wlr_scene_tree* tree = wlr_scene_tree_from_node(node);
        wlr_scene_node* child;
        wl_list_for_each(child, &tree->children, link) {
            ivec2 np = node_pos + ivec2{child->x, child->y};
            if (!walk_scene_tree_back_to_front(child, np, for_each, for_each_data, filter_disabled)) {
                return false;
            }
        }
    }

    return true;
}

bool walk_scene_tree_front_to_back(wlr_scene_node* node, ivec2 node_pos, bool(*for_each)(void*, wlr_scene_node*, ivec2), void* for_each_data, bool filter_disabled)
{
    if (filter_disabled && !node->enabled) return true;

    if (node->type == WLR_SCENE_NODE_TREE) {
        wlr_scene_tree* tree = wlr_scene_tree_from_node(node);
        wlr_scene_node* child;
        wl_list_for_each_reverse(child, &tree->children, link) {
            ivec2 np = node_pos + ivec2{child->x, child->y};
            if (!walk_scene_tree_front_to_back(child, np, for_each, for_each_data, filter_disabled)) {
                return false;
            }
        }
    }

    return for_each(for_each_data, node, node_pos);
}

// -----------------------------------------------------------------------------

vec2 constrain_to_region(const pixman_region32_t* region, vec2 p1, vec2 p2, bool* was_inside)
{
    if (vec2 constrained; (*was_inside = wlr_region_confine(region, p1.x, p1.y, p2.x, p2.y, &constrained.x, &constrained.y))) {
        return constrained;
    }

    i32 nrects;
    const pixman_box32_t* rects = pixman_region32_rectangles(region, &nrects);

    f64 best_dist = INFINITY;
    vec2 best = p2;

    for (i32 i = 0; i < nrects; ++i) {
        pixman_box32_t rect = rects[i];

        vec2 inside = vec2(
            std::clamp(p2.x, f64(rect.x1), f64(std::max(rect.x1, rect.x2 - 1))),
            std::clamp(p2.y, f64(rect.y1), f64(std::max(rect.y1, rect.y2 - 1)))
        );

        f64 dist = glm::distance(p2, inside);
        if (dist < best_dist) {
            best = inside;
            best_dist = dist;
        }
    }

    return best;
}

// -----------------------------------------------------------------------------

wlr_buffer* buffer_from_pixels(wlr_allocator* allocator, wlr_renderer* renderer, u32 upload_format, u32 stride, u32 width, u32 height, const void* data)
{
    auto* upload_texture = wlr_texture_from_pixels(renderer, upload_format, stride, width, height, data);
    defer { wlr_texture_destroy(upload_texture); };

    auto formats = wlr_renderer_get_texture_formats(renderer, WLR_BUFFER_CAP_DMABUF);
    auto* format = wlr_drm_format_set_get(formats, DRM_FORMAT_ARGB8888);

    auto* buffer = wlr_allocator_create_buffer(allocator, width, height, format);

    auto* timeline = wlr_drm_syncobj_timeline_create(wlr_renderer_get_drm_fd(renderer));
    defer { wlr_drm_syncobj_timeline_unref(timeline); };

    wlr_buffer_pass_options buffer_pass_options = {};
    buffer_pass_options.signal_timeline = timeline;
    buffer_pass_options.signal_point = 1;

    auto* rp = wlr_renderer_begin_buffer_pass(renderer, buffer, &buffer_pass_options);

    wlr_render_pass_add_texture(rp, ptr(wlr_render_texture_options {
        .texture = upload_texture,
    }));

    wlr_render_pass_submit(rp);

    bool res = false;
    wlr_drm_syncobj_timeline_check(timeline, 1, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, &res);

    return buffer;
}

// -----------------------------------------------------------------------------

wlr_fbox rect_fill_compute_source_box(ivec2 source_extent, ivec2 target_extent)
{
    f64 source_aspect = f64(source_extent.x) / source_extent.y;
    f64 dest_aspect = f64(target_extent.x) / target_extent.y;

    if (source_aspect >= dest_aspect) {
        // Horizontal will be clipped

        f64 new_horizontal = source_extent.y * dest_aspect;
        f64 offset = (source_extent.x - new_horizontal) / 2;

        return { offset, 0, new_horizontal, f64(source_extent.y) };

    } else {
        // Vertical will be clipped

        f64 new_vertical = source_extent.x / dest_aspect;
        f64 offset = (source_extent.y - new_vertical) / 2;

        return { 0, offset, f64(source_extent.x), new_vertical };
    }
}
