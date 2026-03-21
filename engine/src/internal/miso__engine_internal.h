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
    bool pixel_snap;
} MisoCameraState;

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
};

void miso__engine_request_quit(MisoEngine *engine);
MisoCameraState *miso__camera_get(MisoEngine *engine, MisoCameraId id);
const MisoCameraState *miso__camera_get_const(const MisoEngine *engine, MisoCameraId id);
void miso__camera_get_view_projection(const MisoEngine *engine, MisoCameraId id, float out_matrix[16]);
void miso__render_shutdown(void);

#endif
