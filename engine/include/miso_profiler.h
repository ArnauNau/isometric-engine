#ifndef MISO_PROFILER_H
#define MISO_PROFILER_H

#include "miso_engine.h"
#include "miso_text.h"

#define MISO_PROFILER_HISTORY_COUNT 240
#define MISO_PROFILER_CATEGORY_MAX 64
#define MISO_PROFILER_CATEGORY_NAME_MAX 48
#define MISO_PROFILER_CATEGORY_NONE UINT16_MAX

typedef uint16_t MisoProfilerCategoryId;

typedef enum MisoProfilerLayer { MISO_PROFILER_LAYER_ENGINE = 0, MISO_PROFILER_LAYER_GAME } MisoProfilerLayer;

typedef enum MisoProfilerEngineCategory {
    MISO_PROFILER_ENGINE_EVENTS = 0,
    MISO_PROFILER_ENGINE_FIXED_TICKS,
    MISO_PROFILER_ENGINE_RENDER_WORLD,
    MISO_PROFILER_ENGINE_RENDER_UI,
    MISO_PROFILER_ENGINE_RENDER_DEBUG,
    MISO_PROFILER_ENGINE_RENDERER_ACQUIRE,
    MISO_PROFILER_ENGINE_RENDERER_RECORD,
    MISO_PROFILER_ENGINE_RENDERER_SUBMIT,
    MISO_PROFILER_ENGINE_DEBUG_UI,
    MISO_PROFILER_ENGINE_FRAME_TOTAL,
    MISO_PROFILER_ENGINE_CATEGORY_COUNT
} MisoProfilerEngineCategory;

typedef struct MisoProfilerCategoryInfo {
    bool active;
    MisoProfilerLayer layer;
    MisoProfilerCategoryId parent;
    char name[MISO_PROFILER_CATEGORY_NAME_MAX];
    uint32_t rgba8;
} MisoProfilerCategoryInfo;

typedef struct MisoProfilerFrameSnapshot {
    float category_duration_ms[MISO_PROFILER_CATEGORY_MAX];
    float total_duration_ms;
} MisoProfilerFrameSnapshot;

typedef struct MisoProfilerSnapshot {
    MisoProfilerCategoryInfo categories[MISO_PROFILER_CATEGORY_MAX];
    MisoProfilerFrameSnapshot frames[MISO_PROFILER_HISTORY_COUNT];
    int category_count;
    int newest;
    int count;
    float goal_frame_time_ms;
    float fps_min;
    float fps_avg;
    float fps_max;
} MisoProfilerSnapshot;

MisoResult miso_profiler_init(MisoEngine *engine);
void miso_profiler_shutdown(MisoEngine *engine);

void miso_profiler_frame_start(MisoEngine *engine);
void miso_profiler_frame_end(MisoEngine *engine);
void miso_profiler_begin(MisoEngine *engine, MisoProfilerCategoryId category);
void miso_profiler_end(MisoEngine *engine, MisoProfilerCategoryId category);
void miso_profiler_set_duration(MisoEngine *engine, MisoProfilerCategoryId category, float duration_ms);

bool miso_profiler_register_game_category(MisoEngine *engine,
                                          const char *name,
                                          uint32_t rgba8,
                                          MisoProfilerCategoryId *out_category);
bool miso_profiler_register_game_category_child(MisoEngine *engine,
                                                MisoProfilerCategoryId parent,
                                                const char *name,
                                                uint32_t rgba8,
                                                MisoProfilerCategoryId *out_category);
bool miso_profiler_get_snapshot(const MisoEngine *engine, MisoProfilerSnapshot *out_snapshot);
const char *miso_profiler_get_category_name(const MisoEngine *engine, MisoProfilerCategoryId category);
uint32_t miso_profiler_get_category_color(const MisoEngine *engine, MisoProfilerCategoryId category);
void miso_profiler_get_fps(const MisoEngine *engine, float *out_min, float *out_avg, float *out_max);
float miso_profiler_get_last_frame_time_ms(const MisoEngine *engine);

void miso_profiler_overlay_init(MisoEngine *engine, MisoFontHandle font);
void miso_profiler_overlay_shutdown(MisoEngine *engine);
void miso_profiler_overlay_render(MisoEngine *engine, SDL_FPoint position);

#endif
