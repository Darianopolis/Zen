#include "core.hpp"

void background_destroy(Server* server)
{
    if (server) {
        wlr_buffer_drop(server->background);
    }
}

void background_set(Server* server, const char* path)
{
    background_destroy(server);

    i32 w, h, num_channels;
    stbi_uc* data = stbi_load(path, &w, &h, &num_channels, STBI_rgb_alpha);
    defer { stbi_image_free(data); };

    if (!data) {
        log_error("Failed to load background image: [{}]", path);
        return;
    }

    server->background = buffer_from_pixels(server->allocator, server->renderer, DRM_FORMAT_ABGR8888, 4 * w, w, h, data);

    for (auto* output : server->outputs) {
        background_output_set(output);
    }
}

void background_output_destroy(Output* output)
{
    if (output->background_image) {
        wlr_scene_node_destroy(&output->background_image->node);
    }
}

void background_output_set(Output* output)
{
    auto* server = output->server;

    background_output_destroy(output);

    if (server->background) {
        output->background_image = wlr_scene_buffer_create(server->layers[Strata::background], server->background);

        background_output_position(output);
    }
}

void background_output_position(Output* output)
{
    auto* o = output->wlr_output;
    auto* lo = output->layout_output();

    if (!lo) return;

    if (output->background_image) {
        auto bg = output->server->background;

        wlr_scene_node_set_position(&output->background_image->node, lo->x, lo->y);
        wlr_scene_buffer_set_dest_size(output->background_image, o->width, o->height);

        auto source_box = rect_fill_compute_source_box({bg->width, bg->height}, {o->width, o->height});
        wlr_scene_buffer_set_source_box(output->background_image, &source_box);
    }
}
