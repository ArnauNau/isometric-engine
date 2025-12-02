//
// Created by Arnau Sanz on 16/8/25.
//

#ifndef NAU_ENGINE_CAMERA_H
#define NAU_ENGINE_CAMERA_H

#include "../ecs.h"
#include "../entity.h"
#include <SDL3/SDL.h>

/* Camera constants */
#define CAMERA_MIN_ZOOM 0.5f
#define CAMERA_MAX_ZOOM 5.0f
#define CAMERA_PAN_SPEED 500.0f  // world units per second at zoom=1

typedef struct Camera2D_ {
    SDL_FPoint position; // camera position in world coordinates (screen center)
    // //TODO: change to PositionComponent or
    // TransformComponent or something
    float zoom; // camera zoom level
    SDL_Rect viewport; // camera viewport in screen coordinates
    bool pixel_snap; // snap to integers to avoid blurry lines (subpixel rendering
    // issues)
} Camera2D;

typedef struct Camera2D_Component_ {
    // Entity   entity;               //entity this camera is attached to
    Camera2D camera; // camera data
} Camera2D_Component;

typedef struct SmoothZoom_Component_ {
    float target_zoom; // target zoom level
    float speed; // speed at which the camera zooms in/out
} SmoothZoom_Component;

/* COMPONENTS */
void CAMERA_add(ECSWorld *world, Entity entity, Camera2D camera);

void CAMERA_zoom_apply(ECSWorld *world, Entity entity, float zoom_direction,
                       float speed);

void CAMERA_zoom_set(Camera2D_Component *camera, float zoom,
                     SDL_FPoint mouse_position);

/* MOVEMENT */
// Pan the camera by (dx, dy) direction. Movement speed is inversely proportional
// to zoom level, so panning feels consistent regardless of zoom.
void CAMERA_pan(Camera2D *camera, float dx, float dy, float dt);

/* SYSTEMS */
void CAMERA_smooth_zoom_system(ECSWorld *world, float dt,
                               SDL_FPoint mouse_position);

/*      UTILITIES       */
// NOTE: ISOMETRIC WORLD!!! North orientation is up, West is left, South is down, East is right.
static inline SDL_FPoint cam_world_to_screen(const Camera2D *const c,
                                             const float wx, const float wy) {
    const float cx = (float) c->viewport.x + (float) c->viewport.w * 0.5f;
    const float cy = (float) c->viewport.y + (float) c->viewport.h * 0.5f;
    const float sx = (wx - c->position.x) * c->zoom + cx;
    const float sy = (wy - c->position.y) * c->zoom + cy;
    return (SDL_FPoint){sx, sy};
}

static inline SDL_FPoint cam_screen_to_world(const Camera2D *const c,
                                             const float sx, const float sy) {
    const float cx = (float) c->viewport.x + (float) c->viewport.w * 0.5f;
    const float cy = (float) c->viewport.y + (float) c->viewport.h * 0.5f;
    const float wx = (sx - cx) / c->zoom + c->position.x;
    const float wy = (sy - cy) / c->zoom + c->position.y;
    return (SDL_FPoint){wx, wy};
}

static inline void cam_get_render_params(const Camera2D *const c,
                                         float *const scale, float *const offx,
                                         float *const offy) {
    *scale = c->zoom;
    // offsets are the "pre-scale" logical translation
    const float cx = (float) c->viewport.w * 0.5f;
    const float cy = (float) c->viewport.h * 0.5f;
    *offx = (cx / c->zoom) - c->position.x;
    *offy = (cy / c->zoom) - c->position.y;
    if (c->pixel_snap) {
        *offx = SDL_floorf(*offx);
        *offy = SDL_floorf(*offy);
    }
}

// Build a column-major 4x4 view-projection matrix for GPU rendering
// Combines orthographic projection with camera transform (pan + zoom)
// Coordinate system: (0,0) = top-left of viewport, Y increases downward
static inline void cam_get_view_projection_matrix(const Camera2D *const c,
                                                  float out_matrix[16]) {
    float scale, offx, offy;
    cam_get_render_params(c, &scale, &offx, &offy);

    const float w = (float) c->viewport.w;
    const float h = (float) c->viewport.h;

    // Combined view-projection matrix (column-major for Metal/GL)
    // Ortho projection with Y-flip (screen-space: Y down)
    const float m00 = 2.0f * scale / w;
    const float m11 = -2.0f * scale / h; // Flip Y
    const float m30 = (offx * scale * 2.0f / w) - 1.0f;
    const float m31 = 1.0f - (offy * scale * 2.0f / h); // 1 - ... because of flip

    // Z mapping: 0..1 to 0..1
    constexpr float m22 = 1.0f;
    constexpr float m32 = 0.0f;

    // Column-major order
    out_matrix[0] = m00;
    out_matrix[1] = 0.0f;
    out_matrix[2] = 0.0f;
    out_matrix[3] = 0.0f;

    out_matrix[4] = 0.0f;
    out_matrix[5] = m11;
    out_matrix[6] = 0.0f;
    out_matrix[7] = 0.0f;

    out_matrix[8] = 0.0f;
    out_matrix[9] = 0.0f;
    out_matrix[10] = m22;
    out_matrix[11] = 0.0f;

    out_matrix[12] = m30;
    out_matrix[13] = m31;
    out_matrix[14] = m32;
    out_matrix[15] = 1.0f;
}

#endif // NAU_ENGINE_CAMERA_H
