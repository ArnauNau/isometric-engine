#ifndef MISO_CAMERA_H
#define MISO_CAMERA_H

#include "miso_engine.h"

typedef uint32_t MisoCameraId;

typedef struct MisoVec2 {
    float x;
    float y;
} MisoVec2;

typedef struct MisoViewportRect {
    float x;
    float y;
    float w;
    float h;
} MisoViewportRect;

MisoCameraId miso_camera_create(MisoEngine *engine);
void miso_camera_set_viewport(MisoEngine *engine, MisoCameraId camera_id, int x, int y, int width, int height);

/**
 * Sets a viewport relative to the current drawable size.
 *
 * Values are clamped to 0..1 and resolved to pixels immediately and whenever
 * the window is resized. Use pixel viewports for fixed-size/editor layouts;
 * use normalized viewports for cameras that should scale with the window.
 *
 * \param engine
 * \param camera_id
 * \param x
 * \param y
 * \param width
 * \param height
 *
 * \sa miso_camera_set_viewport
 * \sa MISO_EVENT_WINDOW_RESIZED
 */
void miso_camera_set_viewport_normalized(
    MisoEngine *engine, MisoCameraId camera_id, float x, float y, float width, float height);
void miso_camera_set_position(MisoEngine *engine, MisoCameraId camera_id, float x, float y);
void miso_camera_set_zoom(MisoEngine *engine, MisoCameraId camera_id, float zoom);
void miso_camera_pan(MisoEngine *engine, MisoCameraId camera_id, float dx_world, float dy_world);
void miso_camera_zoom_at_screen(MisoEngine *engine, MisoCameraId camera_id, float wheel_delta, float sx, float sy);
MisoVec2 miso_camera_get_position(const MisoEngine *engine, MisoCameraId camera_id);
float miso_camera_get_zoom(const MisoEngine *engine, MisoCameraId camera_id);

MisoVec2 miso_camera_screen_to_world(const MisoEngine *engine, MisoCameraId camera_id, int sx, int sy);
MisoVec2 miso_camera_world_to_screen(const MisoEngine *engine, MisoCameraId camera_id, float wx, float wy);

#endif
