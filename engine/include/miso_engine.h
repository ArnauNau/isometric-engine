#ifndef MISO_ENGINE_H
#define MISO_ENGINE_H

#include "miso_events.h"

#define MISO_VERSION "0.1.0"
#define MISO_PATH_MAX 1024U

#include <stddef.h>
#include <stdint.h>

typedef struct MisoEngine MisoEngine;

typedef struct MisoConfig {
    int window_width;
    int window_height;
    const char *window_title;
    bool enable_vsync;
    const char *data_root; /* If NULL, engine auto-resolves from the executable base path. */
    int sim_tick_hz;
    int max_sim_steps_per_frame;
} MisoConfig;

typedef enum MisoResult {
    MISO_OK = 0,
    MISO_ERR_INIT,
    MISO_ERR_IO,
    MISO_ERR_GPU,
    MISO_ERR_INVALID_ARG,
    MISO_ERR_NOT_FOUND,
    MISO_ERR_UNSUPPORTED,
    MISO_ERR_OUT_OF_MEMORY
} MisoResult;

typedef struct MisoByteBuffer {
    uint8_t *data;
    size_t size;
} MisoByteBuffer;

typedef void (*MisoSimTickFn)(void *user, float fixed_dt_seconds);

/*
 * Normal frame rendering order is world -> ui -> debug via miso_end_frame().
 *
 * Live window-resize redraws are a separate maintenance path used to avoid
 * stretched stale frames while the OS is interactively resizing the window.
 * That path is intentionally weaker than a full frame contract: clients should
 * only rely on the world layer being kept visually current during live resize.
 * UI/debug may be skipped or may not visibly update until the next normal
 * frame resumes.
 */
typedef struct MisoGameHooks {
    void (*on_event)(void *game_ctx, const MisoEvent *event);
    void (*on_sim_tick)(void *game_ctx, float fixed_dt_seconds);
    void (*on_render_world)(void *game_ctx, const MisoEngine *engine);
    void (*on_render_ui)(void *game_ctx, const MisoEngine *engine);
    void (*on_render_debug)(void *game_ctx, const MisoEngine *engine);
    MisoResult (*on_save)(const void *game_ctx, MisoByteBuffer *out_payload, uint32_t *out_payload_version);
    MisoResult (*on_load)(void *game_ctx, const uint8_t *payload, size_t payload_size, uint32_t payload_version);
    void (*on_reset)(void *game_ctx);
    uint64_t (*on_state_hash)(const void *game_ctx);
} MisoGameHooks;

MisoResult miso_create(const MisoConfig *cfg, MisoEngine **out_engine);
void miso_destroy(MisoEngine *engine);

bool miso_begin_frame(MisoEngine *engine);
void miso_end_frame(MisoEngine *engine);
void miso_get_window_size_pixels(const MisoEngine *engine, int *out_width, int *out_height);
float miso_get_window_pixel_density(const MisoEngine *engine);
bool miso_resolve_asset_path(const MisoEngine *engine, const char *path, char *out_path, size_t out_path_size);
const char *miso_get_data_root(const MisoEngine *engine);

void miso_run_simulation_ticks(MisoEngine *engine, MisoSimTickFn tick_fn, void *user);
float miso_get_real_delta_seconds(const MisoEngine *engine);
float miso_get_interpolation_alpha(const MisoEngine *engine);

MisoResult miso_game_register(MisoEngine *engine, const MisoGameHooks *hooks, void *game_ctx);

bool miso_poll_event(MisoEngine *engine, MisoEvent *out_event);

#endif
