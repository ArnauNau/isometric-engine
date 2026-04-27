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

/**
 * Creates a camera using the current window size as its pixel viewport.
 *
 * Camera ids are one-based handles; 0 means creation failed. Newly created
 * cameras start at world position (0, 0), zoom 1.0, and use pixel snapping for
 * world rendering.
 *
 * \param engine Engine that owns the camera.
 * \return New camera id, or 0 on failure.
 */
MisoCameraId miso_camera_create(MisoEngine *engine);

/**
 * Sets a fixed pixel viewport for a camera.
 *
 * Width and height are clamped to at least 1. Invalid engines or camera ids are
 * ignored. Fixed pixel viewports are not automatically resized with the window.
 *
 * \param engine Engine that owns the camera.
 * \param camera_id Camera to update.
 * \param x Left viewport coordinate in pixels.
 * \param y Top viewport coordinate in pixels.
 * \param width Viewport width in pixels.
 * \param height Viewport height in pixels.
 */
void miso_camera_set_viewport(MisoEngine *engine, MisoCameraId camera_id, int x, int y, int width, int height);

/**
 * Sets a viewport relative to the current drawable size.
 *
 * Values are clamped to 0..1 and resolved to pixels immediately and whenever
 * the window is resized. Use pixel viewports for fixed-size/editor layouts;
 * use normalized viewports for cameras that should scale with the window.
 *
 * \param engine Engine that owns the camera.
 * \param camera_id Camera to update.
 * \param x Normalized left coordinate.
 * \param y Normalized top coordinate.
 * \param width Normalized viewport width.
 * \param height Normalized viewport height.
 *
 * \sa miso_camera_set_viewport
 * \sa MISO_EVENT_WINDOW_RESIZED
 */
void miso_camera_set_viewport_normalized(
    MisoEngine *engine, MisoCameraId camera_id, float x, float y, float width, float height);

/**
 * Sets the camera center in world coordinates.
 *
 * Invalid engines or camera ids are ignored.
 *
 * \param engine Engine that owns the camera.
 * \param camera_id Camera to update.
 * \param x World x coordinate for the camera center.
 * \param y World y coordinate for the camera center.
 */
void miso_camera_set_position(MisoEngine *engine, MisoCameraId camera_id, float x, float y);

/**
 * Sets camera zoom.
 *
 * Zoom is clamped to the current engine range of 0.5..5.0. Invalid engines or
 * camera ids are ignored.
 *
 * \param engine Engine that owns the camera.
 * \param camera_id Camera to update.
 * \param zoom Requested zoom value.
 */
void miso_camera_set_zoom(MisoEngine *engine, MisoCameraId camera_id, float zoom);

/**
 * Offsets the camera center by world-space deltas.
 *
 * Invalid engines or camera ids are ignored.
 *
 * \param engine Engine that owns the camera.
 * \param camera_id Camera to update.
 * \param dx_world World x delta to add.
 * \param dy_world World y delta to add.
 */
void miso_camera_pan(MisoEngine *engine, MisoCameraId camera_id, float dx_world, float dy_world);

/**
 * Changes zoom while preserving the world point under a screen position.
 *
 * Positive wheel deltas zoom in by one step; zero and negative deltas zoom out
 * by one step. The final zoom is clamped to 0.5..5.0.
 *
 * \param engine Engine that owns the camera.
 * \param camera_id Camera to update.
 * \param wheel_delta Mouse wheel delta or signed zoom step.
 * \param sx Screen x coordinate to keep anchored.
 * \param sy Screen y coordinate to keep anchored.
 */
void miso_camera_zoom_at_screen(MisoEngine *engine, MisoCameraId camera_id, float wheel_delta, float sx, float sy);

/**
 * Returns the camera center in world coordinates.
 *
 * Invalid engines or camera ids return (0, 0).
 *
 * \param engine Engine that owns the camera.
 * \param camera_id Camera to query.
 * \return Camera center in world coordinates.
 */
MisoVec2 miso_camera_get_position(const MisoEngine *engine, MisoCameraId camera_id);

/**
 * Returns the camera zoom.
 *
 * Invalid engines or camera ids return 1.0.
 *
 * \param engine Engine that owns the camera.
 * \param camera_id Camera to query.
 * \return Camera zoom value.
 */
float miso_camera_get_zoom(const MisoEngine *engine, MisoCameraId camera_id);

/**
 * Converts a pixel-space screen coordinate to world coordinates for a camera.
 *
 * The conversion uses the camera viewport center and current zoom. Invalid
 * engines or camera ids return (0, 0).
 *
 * \param engine Engine that owns the camera.
 * \param camera_id Camera used for conversion.
 * \param sx Screen x coordinate in pixels.
 * \param sy Screen y coordinate in pixels.
 * \return World coordinate.
 */
MisoVec2 miso_camera_screen_to_world(const MisoEngine *engine, MisoCameraId camera_id, int sx, int sy);

/**
 * Converts a world-space point to pixel-space screen coordinates for a camera.
 *
 * The conversion uses the camera viewport center and current zoom. Invalid
 * engines or camera ids return (0, 0).
 *
 * \param engine Engine that owns the camera.
 * \param camera_id Camera used for conversion.
 * \param wx World x coordinate.
 * \param wy World y coordinate.
 * \return Screen coordinate in pixels.
 */
MisoVec2 miso_camera_world_to_screen(const MisoEngine *engine, MisoCameraId camera_id, float wx, float wy);

#endif
