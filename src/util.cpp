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

    int nrects;
    const pixman_box32_t* rects = pixman_region32_rectangles(region, &nrects);

    double best_dist = INFINITY;
    vec2 best = p2;

    for (int i = 0; i < nrects; ++i) {
        pixman_box32_t rect = rects[i];

        vec2 inside = vec2(
            std::clamp(p2.x, double(rect.x1), double(std::max(rect.x1, rect.x2 - 1))),
            std::clamp(p2.y, double(rect.y1), double(std::max(rect.y1, rect.y2 - 1)))
        );

        double dist = glm::distance(p2, inside);
        if (dist < best_dist) {
            best = inside;
            best_dist = dist;
        }
    }

    return best;
}

// -----------------------------------------------------------------------------

wlr_buffer* buffer_from_pixels(wlr_allocator* allocator, wlr_renderer* renderer, uint32_t upload_format, uint32_t stride, uint32_t width, uint32_t height, const void* data)
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

wlr_box rect_adjust(ivec2 source_extent, ivec2 target_extent, auto compare_op)
{
    // First try to adjust vertical
    double scale = double(target_extent.y) / source_extent.y;
    ivec2 new_size = ivec2(vec2(source_extent) * scale);
    if (compare_op(new_size.x, target_extent.x)) {
        int offset = (new_size.x - target_extent.x) / 2;
        return { -offset, 0, new_size.x, new_size.y };
    }

    // ... then fallback to horizontal
    scale = double(target_extent.x) / source_extent.x;
    new_size = ivec2(vec2(source_extent) * scale);
    int offset = (new_size.y - target_extent.y) / 2;
    return { 0, -offset, new_size.x, new_size.y };
}

wlr_box rect_fit(ivec2 source_extent, ivec2 target_extent)
{
    return rect_adjust(source_extent, target_extent, std::less_equal<int>{});
}

wlr_box rect_fill(ivec2 source_extent, ivec2 target_extent)
{
    return rect_adjust(source_extent, target_extent, std::greater_equal<int>{});
}
