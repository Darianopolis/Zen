#include "pch.hpp"
#include "core.hpp"

void spawn(std::string_view file, std::span<const std::string_view> argv, std::span<const SpawnEnvAction> env_actions)
{
    std::vector<std::string> argv_str;
    for (std::string_view a : argv) argv_str.emplace_back(a);

    std::vector<char*> argv_cstr;
    for (std::string& s : argv_str) argv_cstr.emplace_back(s.data());
    argv_cstr.emplace_back(nullptr);

    log_info("Spawning process [{}] args {}", file, argv);

    if (fork() == 0) {
        for (const SpawnEnvAction& env_action : env_actions) {
            if (env_action.value) {
                setenv(env_action.name, env_action.value, true);
            } else {
                unsetenv(env_action.name);
            }
        }
        std::string file_str{file};
        execvp(file_str.c_str(), argv_cstr.data());
    }
}

// -----------------------------------------------------------------------------

bool walk_scene_tree_back_to_front(wlr_scene_node* node, double sx, double sy, bool(*for_each)(void*, wlr_scene_node*, double, double), void* for_each_data, bool filter_disabled)
{
    if (filter_disabled && !node->enabled) return true;
    if (!for_each(for_each_data, node, sx, sy)) return false;

    if (node->type == WLR_SCENE_NODE_TREE) {
        wlr_scene_tree* tree = wlr_scene_tree_from_node(node);
        wlr_scene_node* child;
        wl_list_for_each(child, &tree->children, link) {
            double nx = sx + child->x;
            double ny = sy + child->y;
            if (!walk_scene_tree_back_to_front(child, nx, ny, for_each, for_each_data, filter_disabled)) {
                return false;
            }
        }
    }

    return true;
}

bool walk_scene_tree_front_to_back(wlr_scene_node* node, double sx, double sy, bool(*for_each)(void*, wlr_scene_node*, double, double), void* for_each_data, bool filter_disabled)
{
    if (filter_disabled && !node->enabled) return true;

    if (node->type == WLR_SCENE_NODE_TREE) {
        wlr_scene_tree* tree = wlr_scene_tree_from_node(node);
        wlr_scene_node* child;
        wl_list_for_each_reverse(child, &tree->children, link) {
            double nx = sx + child->x;
            double ny = sy + child->y;
            if (!walk_scene_tree_front_to_back(child, nx, ny, for_each, for_each_data, filter_disabled)) {
                return false;
            }
        }
    }

    return for_each(for_each_data, node, sx, sy);
}

// -----------------------------------------------------------------------------

Point constrain_to_region(const pixman_region32_t* region, Point p1, Point p2, bool* was_inside)
{
    if (Point constrained; (*was_inside = wlr_region_confine(region, p1.x, p1.y, p2.x, p2.y, &constrained.x, &constrained.y))) {
        return constrained;
    }

    int nrects;
    const pixman_box32_t* rects = pixman_region32_rectangles(region, &nrects);

    double best_dist = INFINITY;
    Point best = p2;

    for (int i = 0; i < nrects; ++i) {
        pixman_box32_t rect = rects[i];

        Point inside = Point(
            std::clamp(p2.x, double(rect.x1), double(std::max(rect.x1, rect.x2 - 1))),
            std::clamp(p2.y, double(rect.y1), double(std::max(rect.y1, rect.y2 - 1)))
        );

        double dist = distance_between(p2, inside);
        if (dist < best_dist) {
            best = inside;
            best_dist = dist;
        }
    }

    return best;
}
