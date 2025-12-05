#include "core.hpp"

void border_manager_create(Server* server)
{
    server->border_manager = new BorderManager {};

    // TODO: Configurable window rules

    auto& rules = server->border_manager->corner_radius_rules;

    rules["io.missioncenter.MissionCenter"] = {{15, 15, 15, 15}};
    rules["org.gnome.Nautilus"]             = {{15, 15, 15, 15}};
    rules["it.mijorus.gearlever"]           = {{15, 15, 15, 15}};

    rules["zenity"] = {{18, 18, 18, 18}};

    rules["firefox"] = EnumMap<int, BorderCorners>::make({
        {BorderCorners::TopLeft,  5},
        {BorderCorners::TopRight, 5},
        {BorderCorners::BottomLeft,  BorderUnset},
        {BorderCorners::BottomRight, BorderUnset},
    });
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

            data[i] = Pixel {
                uint8_t(color.r * alpha * 255.0),
                uint8_t(color.g * alpha * 255.0),
                uint8_t(color.b * alpha * 255.0),
                uint8_t(          alpha * 255.0),
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
void border_apply_rules(Surface* surface)
{
    auto* m = surface->server->border_manager;

    surface->border.show = false;
    surface->border.radius = {m->border_radius, m->border_radius, m->border_radius, m->border_radius};

    if (Toplevel* toplevel = Toplevel::from(surface)) {
        surface->border.show = true;
        auto radius_rules = m->corner_radius_rules.find(toplevel->app_id());
        if (radius_rules != m->corner_radius_rules.end()) {
            surface->border.radius = radius_rules->second;
            for (auto& e : surface->border.radius._data) {
                if (e == BorderUnset) e = m->border_radius;
            }
        }
    } else if (LayerSurface* layer_surface = LayerSurface::from(surface)) {
        std::string_view namespace_ = layer_surface->wlr_layer_surface()->namespace_ ?: "";
        if (namespace_ == "waybar") {
            surface->border.show = true;
        }
    }

    for (auto& e : surface->border.radius._data) {
        if (e == BorderUnset) e = BorderSharp;
    }
}

void borders_create(Surface* surface)
{
    auto* m = surface->server->border_manager;

    for (BorderEdges edge : surface->border.edges.enum_values) {
        surface->border.edges[edge] = wlr_scene_rect_create(surface->scene_tree, 0, 0, color_to_wlroots(m->border_color_unfocused));
    }

    for (BorderCorners corner : surface->border.corners.enum_values) {
        surface->border.corners[corner] = wlr_scene_buffer_create(surface->scene_tree, nullptr);
    }
}

void borders_update(Surface* surface)
{
    auto* m = surface->server->border_manager;

    border_apply_rules(surface);

    // Borders

    wlr_box geom = surface_get_geometry(surface);

    bool focused = get_focused_surface(surface->server) == surface;

    bool show = surface->border.show && surface->wlr_surface->mapped && geom.width && geom.height;
    fvec4 color = focused ? m->border_color_focused : m->border_color_unfocused;

    if (Toplevel* toplevel = Toplevel::from(surface)) {
        show &= !toplevel_is_fullscreen(toplevel);
        color.a *= toplevel_get_opacity(toplevel);
    }

    int border_width = m->border_width;

    EnumMap<wlr_box, BorderEdges> positions;
    positions[BorderEdges::Left]   = { -border_width, 0,  border_width, geom.height  };
    positions[BorderEdges::Right]  = {  geom.width,   0,  border_width, geom.height  };
    positions[BorderEdges::Top]    = {  0, -border_width, geom.width,   border_width };
    positions[BorderEdges::Bottom] = {  0,  geom.height,  geom.width,   border_width };

    // Adjust edges for corner radii

    auto tl = surface->border.radius[BorderCorners::TopLeft];
    auto tr = surface->border.radius[BorderCorners::TopRight];
    auto bl = surface->border.radius[BorderCorners::BottomLeft];
    auto br = surface->border.radius[BorderCorners::BottomRight];

    if (tl != BorderSharp) {
        positions[BorderEdges::Left].y      += tl;
        positions[BorderEdges::Left].height -= tl;
        positions[BorderEdges::Top].x       += tl;
        positions[BorderEdges::Top].width   -= tl;
    } else {
        positions[BorderEdges::Left].y      -= border_width;
        positions[BorderEdges::Left].height += border_width;
    }

    if (tr != BorderSharp) {
        positions[BorderEdges::Right].y      += tr;
        positions[BorderEdges::Right].height -= tr;
        positions[BorderEdges::Top].width    -= tr;
    } else {
        positions[BorderEdges::Right].y      -= border_width;
        positions[BorderEdges::Right].height += border_width;
    }

    if (bl != BorderSharp) {
        positions[BorderEdges::Left].height  -= bl;
        positions[BorderEdges::Bottom].x     += bl;
        positions[BorderEdges::Bottom].width -= bl;
    } else {
        positions[BorderEdges::Left].height  += border_width;
    }

    if (br != BorderSharp) {
        positions[BorderEdges::Right].height -= br;\
        positions[BorderEdges::Bottom].width -= br;
    } else {
        positions[BorderEdges::Right].height += border_width;
    }

    for (BorderEdges edge : surface->border.edges.enum_values) {
        auto* e = surface->border.edges[edge];
        if (show) {
            wlr_scene_node_set_enabled( &e->node, true);
            wlr_scene_node_set_position(&e->node, positions[edge].x, positions[edge].y);
            wlr_scene_rect_set_size(     e, positions[edge].width, positions[edge].height);
            wlr_scene_rect_set_color(    e, color_to_wlroots(color));
        } else {
            wlr_scene_node_set_enabled(&e->node, false);
        }
    }

    // Corners

    EnumMap<ivec2, BorderCorners> src;
    src[BorderCorners::TopLeft]     = { 0,                 0                 };
    src[BorderCorners::TopRight]    = { tr + border_width, 0                 };
    src[BorderCorners::BottomLeft]  = { 0,                 bl + border_width };
    src[BorderCorners::BottomRight] = { br + border_width, br + border_width };

    EnumMap<ivec2, BorderCorners> dst;
    dst[BorderCorners::TopLeft]     = { -border_width,    -border_width     };
    dst[BorderCorners::TopRight]    = {  geom.width - tr, -border_width     };
    dst[BorderCorners::BottomLeft]  = { -border_width,     geom.height - bl };
    dst[BorderCorners::BottomRight] = {  geom.width - br,  geom.height - br };

    for (BorderCorners corner : surface->border.corners.enum_values) {
        auto r = surface->border.radius[corner];
        auto* c = surface->border.corners[corner];
        if (show && r != BorderSharp) {
            auto outer_radius = r + border_width;

            auto& cbs = borders_get_corner_buffers(surface->server, outer_radius);
            auto& cb = focused ? cbs.focused : cbs.unfocused;

            wlr_scene_node_set_enabled(    &c->node, true);
            wlr_scene_node_set_position(   &c->node, dst[corner].x, dst[corner].y);
            wlr_scene_buffer_set_buffer(    c, cb.buffer);
            wlr_scene_buffer_set_dest_size( c, outer_radius, outer_radius);
            wlr_scene_buffer_set_source_box(c, ptr(wlr_fbox {
                .x = float(src[corner].x),
                .y = float(src[corner].y),
                .width  = float(outer_radius),
                .height = float(outer_radius),
            }));
        } else {
            wlr_scene_node_set_enabled(&c->node, false);
        }
    }
}
