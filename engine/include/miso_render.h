#ifndef MISO_RENDER_H
#define MISO_RENDER_H

#include "miso_camera.h"
#include "miso_engine.h"

#include <stdint.h>

typedef uint32_t MisoTextureHandle;
typedef uint32_t MisoFontHandle;

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

typedef struct MisoSpriteInstance {
    float x;
    float y;
    float z;
    float flags;
    float w;
    float h;
    float tile_x;
    float tile_y;
    float u;
    float v;
    float uw;
    float vh;
} MisoSpriteInstance;

typedef struct MisoWorldVertex {
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
} MisoWorldVertex;

MisoResult miso_render_load_texture(const MisoEngine *engine, const char *path, MisoTextureHandle *out_texture);
void miso_render_destroy_texture(const MisoEngine *engine, MisoTextureHandle texture);
MisoResult
miso_render_load_font(const MisoEngine *engine, const char *path, float point_size, MisoFontHandle *out_font);
void miso_render_destroy_font(const MisoEngine *engine, MisoFontHandle font);
bool miso_render_get_frame_stats(const MisoEngine *engine, MisoRenderFrameStats *out_stats);

void miso_render_begin_world(const MisoEngine *engine, MisoCameraId camera_id);
void miso_render_set_water_params(const MisoEngine *engine, float time, float speed, float amplitude, float phase);
void miso_render_submit_sprites(const MisoEngine *engine,
                                MisoTextureHandle texture,
                                const MisoSpriteInstance *instances,
                                int count);
void miso_render_submit_world_geometry(const MisoEngine *engine, const MisoWorldVertex *vertices, int count);
void miso_render_end_world(const MisoEngine *engine);

void miso_render_begin_ui(const MisoEngine *engine);
void miso_render_submit_ui_rect(const MisoEngine *engine, float x, float y, float w, float h, uint32_t rgba8);
void miso_render_submit_ui_text(
    const MisoEngine *engine, MisoFontHandle font, const char *text, float x, float y, uint32_t rgba8);
void miso_render_end_ui(const MisoEngine *engine);

#endif
