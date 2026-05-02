#ifndef MISO_RENDER_DIAGNOSTICS_H
#define MISO_RENDER_DIAGNOSTICS_H

#include "miso_engine.h"
#include "miso_render.h"

/*
 * Advanced renderer diagnostics and tuning.
 * Not required for normal gameplay rendering.
 * Exposes performance counters and advanced runtime behavior controls.
 * Use only if you understand the tradeoffs (presentation pacing, latency,
 * dropped frames, benchmark semantics).
 * This surface may evolve more frequently than the core render API.
 */

typedef enum MisoRenderPresentMode {
    MISO_RENDER_PRESENT_IMMEDIATE = 0,
    MISO_RENDER_PRESENT_VSYNC,
    MISO_RENDER_PRESENT_MAILBOX
} MisoRenderPresentMode;

typedef enum MisoRenderVSyncAcquireMode {
    MISO_RENDER_VSYNC_ACQUIRE_BLOCKING = 0,
    MISO_RENDER_VSYNC_ACQUIRE_PASSTHROUGH
} MisoRenderVSyncAcquireMode;

typedef enum MisoRenderStatsQueueKind {
    MISO_RENDER_STATS_QUEUE_SPRITE = 0,
    MISO_RENDER_STATS_QUEUE_WORLD_GEOMETRY,
    MISO_RENDER_STATS_QUEUE_LINE,
    MISO_RENDER_STATS_QUEUE_UI_GEOMETRY,
    MISO_RENDER_STATS_QUEUE_UI_TEXT,
    MISO_RENDER_STATS_QUEUE_COUNT
} MisoRenderStatsQueueKind;

typedef enum MisoRenderStatsStreamKind {
    MISO_RENDER_STATS_STREAM_SPRITE = 0,
    MISO_RENDER_STATS_STREAM_WORLD_GEOMETRY,
    MISO_RENDER_STATS_STREAM_LINE,
    MISO_RENDER_STATS_STREAM_UI_GEOMETRY,
    MISO_RENDER_STATS_STREAM_UI_TEXT_VERT,
    MISO_RENDER_STATS_STREAM_UI_TEXT_INDEX,
    MISO_RENDER_STATS_STREAM_COUNT
} MisoRenderStatsStreamKind;

typedef struct MisoRenderQueueStats {
    uint32_t cmd_count;
    uint32_t draw_calls;
} MisoRenderQueueStats;

typedef struct MisoRenderPassStats {
    uint32_t begin_calls;
    uint32_t end_calls;
    uint32_t world_passes;
    uint32_t ui_passes;
} MisoRenderPassStats;

typedef struct MisoRenderTimingStats {
    float frame_cpu_ms;
    float acquire_swapchain_ms;
    float record_commands_ms;
    float submit_ms;
} MisoRenderTimingStats;

typedef struct MisoRenderStreamStats {
    uint32_t used_bytes;
    uint32_t peak_bytes;
    uint32_t capacity_bytes;
    uint32_t uploaded_bytes;
    uint32_t overflow_count;
} MisoRenderStreamStats;

typedef struct MisoRenderFrameStats {
    MisoRenderQueueStats queues[MISO_RENDER_STATS_QUEUE_COUNT];
    MisoRenderPassStats passes;
    MisoRenderTimingStats timing;
    uint32_t render_pass_count;
    uint32_t draw_calls_world;
    uint32_t draw_calls_ui;
    uint32_t draw_calls_lines;
    uint32_t uploaded_bytes_sprite;
    uint32_t uploaded_bytes_world_geo;
    uint32_t uploaded_bytes_ui_geo;
    uint32_t uploaded_bytes_ui_text;
    uint32_t uploaded_bytes_line;
    uint32_t uploaded_bytes_total;
    uint32_t instances_submitted;
    uint32_t line_vertices_submitted;
    uint32_t texture_upload_count;
    uint32_t texture_upload_bytes;
    uint32_t transient_buffer_creations;
    MisoRenderStreamStats streams[MISO_RENDER_STATS_STREAM_COUNT];
} MisoRenderFrameStats;

/**
 * Copies the most recent renderer frame statistics.
 *
 * Returns false and clears \p out_stats when no valid renderer snapshot is
 * available. \p out_stats must be non-NULL.
 *
 * \param engine Engine whose renderer is active.
 * \param out_stats Receives the latest frame statistics.
 * \return true if statistics were copied.
 */
bool miso_render_diag_get_frame_stats(const MisoEngine *engine, MisoRenderFrameStats *out_stats);

/**
 * Returns the current GPU present mode reported by the renderer backend.
 *
 * \param engine Engine whose renderer is active.
 * \return Current present mode.
 */
MisoRenderPresentMode miso_render_diag_get_present_mode(const MisoEngine *engine);

/**
 * Returns how the renderer acquires swapchain images while using vsync.
 *
 * \param engine Engine whose renderer is active.
 * \return Current vsync acquire mode.
 */
MisoRenderVSyncAcquireMode miso_render_diag_get_vsync_acquire_mode(const MisoEngine *engine);

/**
 * Returns the current maximum number of GPU frames allowed in flight.
 *
 * \param engine Engine whose renderer is active.
 * \return Allowed frame count.
 */
uint32_t miso_render_diag_get_allowed_frames_in_flight(const MisoEngine *engine);

/**
 * Returns whether transient GPU upload submission is suppressed for diagnostics.
 *
 * \param engine Engine whose renderer is active.
 * \return true if upload suppression is enabled.
 */
bool miso_render_diag_get_upload_suppressed(const MisoEngine *engine);

/**
 * Sets the renderer present mode.
 *
 * Unsupported enum values fall back to vsync. Present-mode changes affect
 * pacing/latency and are primarily intended for diagnostics and benchmarks.
 *
 * \param engine Engine whose renderer is active.
 * \param mode Present mode to request.
 */
void miso_render_tune_set_present_mode(const MisoEngine *engine, MisoRenderPresentMode mode);

/**
 * Sets how the renderer acquires swapchain images when using vsync.
 *
 * This is an advanced pacing control and may affect benchmark interpretation.
 *
 * \param engine Engine whose renderer is active.
 * \param mode Acquire mode to request.
 */
void miso_render_tune_set_vsync_acquire_mode(const MisoEngine *engine, MisoRenderVSyncAcquireMode mode);

/**
 * Sets the maximum number of GPU frames allowed in flight.
 *
 * The backend validates the value; false means it was rejected and no change
 * should be assumed.
 *
 * \param engine Engine whose renderer is active.
 * \param allowed_frames_in_flight Requested in-flight frame count.
 * \return true if the backend accepted the value.
 */
bool miso_render_tune_set_allowed_frames_in_flight(const MisoEngine *engine, uint32_t allowed_frames_in_flight);

/**
 * Enables or disables upload suppression for diagnostics.
 *
 * This is not a normal gameplay setting; it is intended to isolate upload costs
 * during renderer profiling.
 *
 * \param engine Engine whose renderer is active.
 * \param enabled Whether upload suppression should be enabled.
 */
void miso_render_tune_set_upload_suppressed(const MisoEngine *engine, bool enabled);

/**
 * Draws a native backend texture into UI space for debugging.
 *
 * \p native_texture must be a renderer-backend texture pointer compatible with
 * the active renderer. NULL textures are ignored.
 *
 * \param engine Engine whose renderer is active.
 * \param native_texture Backend texture pointer.
 * \param x UI x coordinate in pixels.
 * \param y UI y coordinate in pixels.
 * \param width Draw width in pixels.
 * \param height Draw height in pixels.
 */
void miso_render_diag_submit_ui_texture_debug(
    const MisoEngine *engine, void *native_texture, float x, float y, float width, float height);

/**
 * Draws an engine texture handle into UI space for debugging.
 *
 * Invalid texture handles are ignored. This is intended for diagnostics and
 * should not be used as normal gameplay UI image rendering.
 *
 * \param engine Engine whose renderer is active.
 * \param texture Engine texture handle.
 * \param x UI x coordinate in pixels.
 * \param y UI y coordinate in pixels.
 * \param width Draw width in pixels.
 * \param height Draw height in pixels.
 */
void miso_render_diag_submit_ui_texture_handle_debug(
    const MisoEngine *engine, MisoTextureHandle texture, float x, float y, float width, float height);

/**
 * Submits sprites using a native backend texture pointer.
 *
 * This bypasses MisoTextureHandle lookup and is intended for diagnostics or
 * renderer integration experiments. NULL textures, NULL instances, and
 * non-positive counts are ignored. The instance array must not alias
 * renderer-owned upload storage.
 *
 * \param engine Engine whose renderer is active.
 * \param native_texture Backend texture pointer.
 * \param instances Sprite instance array.
 * \param count Number of sprite instances.
 */
void miso_render_diag_submit_native_sprites(const MisoEngine *engine,
                                            void *native_texture,
                                            const MisoSpriteInstance *restrict instances,
                                            int count);

#endif
