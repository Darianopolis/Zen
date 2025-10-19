#include "pch.hpp"
#include "core.hpp"

void spawn(const char* file, std::span<const std::string_view> argv, std::span<const SpawnEnvAction> env_actions)
{
    std::vector<std::string> argv_str;
    for (std::string_view a : argv) argv_str.emplace_back(a);

    std::vector<char*> argv_cstr;
    for (std::string& s : argv_str) argv_cstr.emplace_back(s.data());
    argv_cstr.emplace_back(nullptr);

    {
        std::string args_preview = "";
        for (std::string_view a : argv) {
            if (a.empty()) continue;
            if (!args_preview.empty()) args_preview += ", ";
            args_preview += std::format("\"{}\"", a);
        }

        log_info("Spawning process [{}] args [{}]", file, args_preview);
    }

    if (fork() == 0) {
        for (const SpawnEnvAction& env_action : env_actions) {
            if (env_action.value) {
                setenv(env_action.name, env_action.value, true);
            } else {
                unsetenv(env_action.name);
            }
        }
        execvp(file, argv_cstr.data());
    }
}

// -----------------------------------------------------------------------------

bool walk_scene_tree_back_to_front(wlr_scene_node* node, double sx, double sy, bool(*for_each)(void*, wlr_scene_node*, double, double), void* for_each_data)
{
    if (!for_each(for_each_data, node, sx, sy)) return false;

    if (node->type == WLR_SCENE_NODE_TREE) {
        wlr_scene_tree* tree = wlr_scene_tree_from_node(node);
        wlr_scene_node* child;
        wl_list_for_each(child, &tree->children, link) {
            double nx = sx + child->x;
            double ny = sy + child->y;
            if (!walk_scene_tree_back_to_front(child, nx, ny, for_each, for_each_data)) {
                return false;
            }
        }
    }

    return true;
}

bool walk_scene_tree_front_to_back(wlr_scene_node* node, double sx, double sy, bool(*for_each)(void*, wlr_scene_node*, double, double), void* for_each_data)
{
    if (node->type == WLR_SCENE_NODE_TREE) {
        wlr_scene_tree* tree = wlr_scene_tree_from_node(node);
        wlr_scene_node* child;
        wl_list_for_each_reverse(child, &tree->children, link) {
            double nx = sx + child->x;
            double ny = sy + child->y;
            if (!walk_scene_tree_front_to_back(child, nx, ny, for_each, for_each_data)) {
                return false;
            }
        }
    }

    return for_each(for_each_data, node, sx, sy);
}
