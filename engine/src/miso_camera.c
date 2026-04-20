#include "miso_camera.h"

#include "internal/miso__engine_internal.h"
#include "internal/miso__math.h"

#include <SDL3/SDL.h>

static void
miso__resolve_normalized_viewport(MisoCameraState *const camera, const int pixel_width, const int pixel_height) {
    if (!camera || pixel_width <= 0 || pixel_height <= 0) {
        return;
    }

    const float x0 = SDL_clamp(camera->normalized_viewport.x, 0.0f, 1.0f);
    const float y0 = SDL_clamp(camera->normalized_viewport.y, 0.0f, 1.0f);
    const float x1 = SDL_clamp(camera->normalized_viewport.x + camera->normalized_viewport.w, 0.0f, 1.0f);
    const float y1 = SDL_clamp(camera->normalized_viewport.y + camera->normalized_viewport.h, 0.0f, 1.0f);

    const int left = SDL_clamp((int)SDL_lroundf(x0 * (float)pixel_width), 0, pixel_width - 1);
    const int top = SDL_clamp((int)SDL_lroundf(y0 * (float)pixel_height), 0, pixel_height - 1);
    const int right = SDL_clamp((int)SDL_lroundf(x1 * (float)pixel_width), left + 1, pixel_width);
    const int bottom = SDL_clamp((int)SDL_lroundf(y1 * (float)pixel_height), top + 1, pixel_height);

    camera->viewport.x = left;
    camera->viewport.y = top;
    camera->viewport.w = right - left;
    camera->viewport.h = bottom - top;
}

void miso__camera_resolve_normalized_viewports(MisoEngine *const engine,
                                               const int pixel_width,
                                               const int pixel_height) {
    if (!engine) {
        return;
    }
    for (uint32_t i = 0; i < engine->camera_count; i++) {
        MisoCameraState *const camera = &engine->cameras[i];
        if (camera->used && camera->viewport_normalized) {
            miso__resolve_normalized_viewport(camera, pixel_width, pixel_height);
        }
    }
}

void miso_camera_set_viewport(MisoEngine *engine, MisoCameraId camera_id, int x, int y, int width, int height) {
    MisoCameraState *camera = miso__camera_get(engine, camera_id);
    if (!camera) {
        return;
    }
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }
    camera->viewport_normalized = false;
    camera->viewport.x = x;
    camera->viewport.y = y;
    camera->viewport.w = width;
    camera->viewport.h = height;
}

void miso_camera_set_viewport_normalized(
    MisoEngine *engine, MisoCameraId camera_id, float x, float y, float width, float height) {
    MisoCameraState *camera = miso__camera_get(engine, camera_id);
    if (!camera) {
        return;
    }
    camera->viewport_normalized = true;
    camera->normalized_viewport.x = x;
    camera->normalized_viewport.y = y;
    camera->normalized_viewport.w = width;
    camera->normalized_viewport.h = height;
    miso__resolve_normalized_viewport(camera, engine->config.window_width, engine->config.window_height);
}

void miso_camera_set_position(MisoEngine *engine, MisoCameraId camera_id, float x, float y) {
    MisoCameraState *camera = miso__camera_get(engine, camera_id);
    if (!camera) {
        return;
    }
    camera->x = x;
    camera->y = y;
}

void miso_camera_set_zoom(MisoEngine *engine, MisoCameraId camera_id, float zoom) {
    MisoCameraState *camera = miso__camera_get(engine, camera_id);
    if (!camera) {
        return;
    }
    if (zoom < 0.5f) {
        zoom = 0.5f;
    }
    if (zoom > 5.0f) {
        zoom = 5.0f;
    }
    camera->zoom = zoom;
}

void miso_camera_pan(MisoEngine *engine, MisoCameraId camera_id, float dx_world, float dy_world) {
    MisoCameraState *camera = miso__camera_get(engine, camera_id);
    if (!camera) {
        return;
    }
    camera->x += dx_world;
    camera->y += dy_world;
}

void miso_camera_zoom_at_screen(MisoEngine *engine, MisoCameraId camera_id, float wheel_delta, float sx, float sy) {
    MisoCameraState *camera = miso__camera_get(engine, camera_id);
    if (!camera) {
        return;
    }

    const float prev_zoom = camera->zoom;
    const float zoom_step = wheel_delta > 0.0f ? 1.1f : 0.9f;
    float new_zoom = prev_zoom * zoom_step;
    if (new_zoom < 0.5f) {
        new_zoom = 0.5f;
    }
    if (new_zoom > 5.0f) {
        new_zoom = 5.0f;
    }

    const float cx = (float)camera->viewport.x + (float)camera->viewport.w * 0.5f;
    const float cy = (float)camera->viewport.y + (float)camera->viewport.h * 0.5f;

    const float world_x = (sx - cx) / prev_zoom + camera->x;
    const float world_y = (sy - cy) / prev_zoom + camera->y;

    camera->zoom = new_zoom;
    camera->x = world_x - (sx - cx) / camera->zoom;
    camera->y = world_y - (sy - cy) / camera->zoom;
}

MisoVec2 miso_camera_get_position(const MisoEngine *engine, MisoCameraId camera_id) {
    const MisoCameraState *camera = miso__camera_get_const(engine, camera_id);
    if (!camera) {
        return (MisoVec2){0.0f, 0.0f};
    }
    return (MisoVec2){camera->x, camera->y};
}

float miso_camera_get_zoom(const MisoEngine *engine, MisoCameraId camera_id) {
    const MisoCameraState *camera = miso__camera_get_const(engine, camera_id);
    if (!camera) {
        return 1.0f;
    }
    return camera->zoom;
}

MisoVec2 miso_camera_screen_to_world(const MisoEngine *engine, MisoCameraId camera_id, int sx, int sy) {
    const MisoCameraState *camera = miso__camera_get_const(engine, camera_id);
    if (!camera) {
        return (MisoVec2){0.0f, 0.0f};
    }

    const float cx = (float)camera->viewport.x + (float)camera->viewport.w * 0.5f;
    const float cy = (float)camera->viewport.y + (float)camera->viewport.h * 0.5f;

    const float world_x = ((float)sx - cx) / camera->zoom + camera->x;
    const float world_y = ((float)sy - cy) / camera->zoom + camera->y;

    return (MisoVec2){world_x, world_y};
}

MisoVec2 miso_camera_world_to_screen(const MisoEngine *engine, MisoCameraId camera_id, float wx, float wy) {
    const MisoCameraState *camera = miso__camera_get_const(engine, camera_id);
    if (!camera) {
        return (MisoVec2){0.0f, 0.0f};
    }

    const float cx = (float)camera->viewport.x + (float)camera->viewport.w * 0.5f;
    const float cy = (float)camera->viewport.y + (float)camera->viewport.h * 0.5f;

    const float sx = (wx - camera->x) * camera->zoom + cx;
    const float sy = (wy - camera->y) * camera->zoom + cy;

    return (MisoVec2){sx, sy};
}
