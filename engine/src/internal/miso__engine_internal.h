#ifndef MISO__ENGINE_INTERNAL_H
#define MISO__ENGINE_INTERNAL_H

#include "miso_camera.h"
#include "miso_engine.h"

#include <SDL3/SDL.h>

typedef struct MisoCameraState {
    bool used;
    float x;
    float y;
    float zoom;
    SDL_Rect viewport;
    bool viewport_normalized;
    MisoViewportRect normalized_viewport;
    bool pixel_snap;
} MisoCameraState;

typedef struct MisoProfilerState MisoProfilerState;

struct MisoEngine {
    MisoConfig config;
    SDL_Window *window;
    bool running;

    char data_root[MISO_PATH_MAX];
    bool has_data_root;

    uint64_t perf_frequency;
    uint64_t last_counter;
    float real_dt_seconds;
    double sim_accumulator;

    MisoGameHooks game_hooks;
    void *game_ctx;
    bool game_registered;

    MisoCameraState *cameras;
    uint32_t camera_capacity;
    uint32_t camera_count;

    MisoProfilerState *profiler;

    bool frame_in_progress;
    bool render_in_progress;
    bool rendered_from_event_watch_this_frame;
    bool has_applied_resize;
    int applied_resize_width;
    int applied_resize_height;
    bool has_notified_resize;
    int notified_resize_width;
    int notified_resize_height;
    SDL_AtomicInt pending_resize_width;
    SDL_AtomicInt pending_resize_height;
    SDL_AtomicInt pending_resize_dirty;
};

void miso__engine_request_quit(MisoEngine *engine);
void miso__engine_apply_resize_if_needed(MisoEngine *engine, int pixel_width, int pixel_height);
bool miso__engine_should_dispatch_resize_event(MisoEngine *engine, int pixel_width, int pixel_height);
void miso__camera_resolve_normalized_viewports(MisoEngine *engine, int pixel_width, int pixel_height);
MisoCameraState *miso__camera_get(MisoEngine *engine, MisoCameraId id);
const MisoCameraState *miso__camera_get_const(const MisoEngine *engine, MisoCameraId id);
void miso__camera_get_view_projection(const MisoEngine *engine, MisoCameraId id, float out_matrix[16]);
void miso__render_shutdown(void);

#endif
