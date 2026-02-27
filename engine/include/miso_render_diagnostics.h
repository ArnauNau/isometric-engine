#ifndef MISO_RENDER_DIAGNOSTICS_H
#define MISO_RENDER_DIAGNOSTICS_H

#include "miso_engine.h"
#include "miso_render.h"

#include <stdbool.h>
#include <stdint.h>

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

bool miso_render_diag_get_frame_stats(const MisoEngine *engine, MisoRenderFrameStats *out_stats);
MisoRenderPresentMode miso_render_diag_get_present_mode(const MisoEngine *engine);
MisoRenderVSyncAcquireMode miso_render_diag_get_vsync_acquire_mode(const MisoEngine *engine);
uint32_t miso_render_diag_get_allowed_frames_in_flight(const MisoEngine *engine);
bool miso_render_diag_get_upload_suppressed(const MisoEngine *engine);

void miso_render_tune_set_present_mode(const MisoEngine *engine, MisoRenderPresentMode mode);
void miso_render_tune_set_vsync_acquire_mode(const MisoEngine *engine, MisoRenderVSyncAcquireMode mode);
bool miso_render_tune_set_allowed_frames_in_flight(const MisoEngine *engine, uint32_t allowed_frames_in_flight);
void miso_render_tune_set_upload_suppressed(const MisoEngine *engine, bool enabled);

void miso_render_diag_submit_ui_texture_debug(
    const MisoEngine *engine, void *native_texture, float x, float y, float width, float height);
void miso_render_diag_submit_native_sprites(const MisoEngine *engine,
                                            void *native_texture,
                                            const MisoSpriteInstance *instances,
                                            int count);

#endif
