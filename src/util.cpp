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