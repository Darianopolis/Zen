#include "core.hpp"

void border_manager_create(Server* server)
{
    server->border_manager = new BorderManager {};

    // TODO: Configurable window rules

    for (auto& app_id : {
        "io.missioncenter.MissionCenter"sv,
        "org.gnome.Nautilus"sv,
        "it.mijorus.gearlever"sv,
    }) {
        auto& radii = server->border_manager->corner_radius_rules[app_id];
        for (auto& v : radii._data) v = 15;
    }

    {
        auto& radii = server->border_manager->corner_radius_rules["firefox"];
        radii[BorderCorners::TopLeft] = 5;
        radii[BorderCorners::TopRight] = 5;
    }
}

static
void borders_update_corner_buffer(Server* server, int radius, fvec4 color, BorderManager::CornerBuffer& cb)
{
    auto* m = server->border_manager;

    if (cb.buffer && color == cb.color && m->border_width == cb.width) return;

    struct Pixel { uint8_t r, g, b, a; };
    int border_width = m->border_width;

    int width = radius * 2;
    int height = radius * 2;

    std::vector<Pixel> data;
    data.resize(width * height);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int i = x + y * width;

            vec2 pos = vec2(x, y) + vec2(0.5);

            double dist = glm::length(pos - vec2(radius, radius));
            double dist_from_border_center = std::abs(dist - (radius - (border_width / 2.0)));
            double alpha = 1.0 - std::clamp((dist_from_border_center - (border_width / 2.0)) + 0.5, 0.0, 1.0);

            alpha *= color.a;

            data[i] = Pixel{
                uint8_t(color.r * alpha * 255.0),
                uint8_t(color.g * alpha * 255.0),
                uint8_t(color.b * alpha * 255.0),
                uint8_t(          alpha * 255.0)
            };
        }
    }

    cb.color = color;
    cb.width = border_width;

    log_warn("Creating new corner buffer with: radius = {}, width = {}, color = {}", radius, border_width, glm::to_string(color));

    if (cb.buffer) {
        log_warn("Destroying old buffer");
        wlr_buffer_drop(cb.buffer);
    }

    cb.buffer = buffer_from_pixels(server->allocator, server->renderer, DRM_FORMAT_ABGR8888, width * 4, width, height, data.data());
}

static
const BorderManager::CornerBuffers& borders_get_corner_buffers(Server* server, int radius)
{
    auto* m = server->border_manager;

    auto& cbs = m->corner_cache[radius];

    borders_update_corner_buffer(server, radius, m->border_color_focused, cbs.focused);
    borders_update_corner_buffer(server, radius, m->border_color_unfocused, cbs.unfocused);

    return cbs;
}

void border_manager_destroy(Server* server)
{
    for (auto&[_, cbs] : server->border_manager->corner_cache) {
        if (cbs.focused.buffer) {
            wlr_buffer_drop(cbs.focused.buffer);
        }
        if (cbs.unfocused.buffer) {
            wlr_buffer_drop(cbs.unfocused.buffer);
        }
    }

    delete server->border_manager;
}

static
void border_apply_rules(Toplevel* toplevel)
{
    std::string_view app_id = toplevel->xdg_toplevel()->app_id ?: "";

    auto* m = toplevel->server->border_manager;

    auto radius_rules = m->corner_radius_rules.find(app_id);
    if (radius_rules != m->corner_radius_rules.end()) {
        toplevel->border.radius = radius_rules->second;
    } else {
        toplevel->border.radius = {m->border_radius, m->border_radius, m->border_radius, m->border_radius};
    }
}

void borders_create(Toplevel* toplevel)
{
    auto* m = toplevel->server->border_manager;

    for (BorderEdges edge : toplevel->border.edges.enum_values) {
        toplevel->border.edges[edge] = wlr_scene_rect_create(toplevel->scene_tree, 0, 0, color_to_wlroots(m->border_color_unfocused));
    }

    for (BorderCorners corner : toplevel->border.corners.enum_values) {
        toplevel->border.corners[corner] = wlr_scene_buffer_create(toplevel->scene_tree, nullptr);
    }
}

void borders_update(Toplevel* toplevel)
{
    auto* m = toplevel->server->border_manager;

    border_apply_rules(toplevel);

    // Borders

    wlr_box geom = surface_get_geometry(toplevel);

    bool show = geom.width && geom.height;
    show &= !toplevel_is_fullscreen(toplevel);

    bool focused = get_focused_surface(toplevel->server) == toplevel;

    fvec4 color = focused ? m->border_color_focused : m->border_color_unfocused;
    color.a *= toplevel_get_opacity(toplevel);

    int border_width = m->border_width;

    EnumMap<wlr_box, BorderEdges> positions;
    positions[BorderEdges::Left]   = { -border_width, 0,  border_width, geom.height  };
    positions[BorderEdges::Right]  = {  geom.width,   0,  border_width, geom.height  };
    positions[BorderEdges::Top]    = {  0, -border_width, geom.width,   border_width };
    positions[BorderEdges::Bottom] = {  0,  geom.height,  geom.width,   border_width };

    {
        // Adjust for corner radius

        auto tl = toplevel->border.radius[BorderCorners::TopLeft];
        auto tr = toplevel->border.radius[BorderCorners::TopRight];
        auto br = toplevel->border.radius[BorderCorners::BottomRight];
        auto bl = toplevel->border.radius[BorderCorners::BottomLeft];

        positions[BorderEdges::Left].y += tl;
        positions[BorderEdges::Left].height -= tl + bl;

        positions[BorderEdges::Right].y += tr;
        positions[BorderEdges::Right].height -= tr + br;

        positions[BorderEdges::Top].x += tl;
        positions[BorderEdges::Top].width -= (tl + tr);

        positions[BorderEdges::Bottom].x += bl;
        positions[BorderEdges::Bottom].width -= (bl + br);
    }

    for (BorderEdges edge : toplevel->border.edges.enum_values) {
        if (show) {
            wlr_scene_node_set_enabled(&toplevel->border.edges[edge]->node, true);
            wlr_scene_node_set_position(&toplevel->border.edges[edge]->node, positions[edge].x, positions[edge].y);
            wlr_scene_rect_set_size(toplevel->border.edges[edge], positions[edge].width, positions[edge].height);
            wlr_scene_rect_set_color(toplevel->border.edges[edge], color_to_wlroots(color));
        } else {
            wlr_scene_node_set_enabled(&toplevel->border.edges[edge]->node, false);
        }
    }

    // Corners

    // Inner radii
    auto i_tr = toplevel->border.radius[BorderCorners::TopRight];
    auto i_br = toplevel->border.radius[BorderCorners::BottomRight];
    auto i_bl = toplevel->border.radius[BorderCorners::BottomLeft];

    // Outer radii
    auto o_tr = i_tr + border_width;
    auto o_br = i_br + border_width;
    auto o_bl = i_bl + border_width;

    EnumMap<ivec2, BorderCorners> src;
    src[BorderCorners::TopLeft]     = { 0,    0    };
    src[BorderCorners::TopRight]    = { o_tr, 0    };
    src[BorderCorners::BottomRight] = { o_br, o_br };
    src[BorderCorners::BottomLeft]  = { 0,    o_bl };

    EnumMap<ivec2, BorderCorners> dst;
    dst[BorderCorners::TopLeft]     = { -border_width,      -border_width       };
    dst[BorderCorners::TopRight]    = {  geom.width - i_tr, -border_width       };
    dst[BorderCorners::BottomRight] = {  geom.width - i_br,  geom.height - i_br };
    dst[BorderCorners::BottomLeft]  = { -border_width,       geom.height - i_bl };

    for (BorderCorners corner : toplevel->border.corners.enum_values) {
        if (show) {
            auto outer_radius = toplevel->border.radius[corner] + border_width;

            auto& cbs = borders_get_corner_buffers(toplevel->server, outer_radius);
            auto& cb = focused ? cbs.focused : cbs.unfocused;

            wlr_scene_node_set_enabled(    &toplevel->border.corners[corner]->node, true);
            wlr_scene_node_set_position(   &toplevel->border.corners[corner]->node, dst[corner].x, dst[corner].y);
            wlr_scene_buffer_set_buffer(    toplevel->border.corners[corner], cb.buffer);
            wlr_scene_buffer_set_dest_size( toplevel->border.corners[corner], outer_radius, outer_radius);
            wlr_scene_buffer_set_source_box(toplevel->border.corners[corner], ptr(wlr_fbox {
                .x = float(src[corner].x),
                .y = float(src[corner].y),
                .width = float(outer_radius),
                .height = float(outer_radius),
            }));
        } else {
            wlr_scene_node_set_enabled(&toplevel->border.corners[corner]->node, false);
        }
    }
}
