#include "miso_engine.h"

#include "internal/miso__engine_internal.h"
#include "internal/miso__paths.h"
#include "internal/miso__renderer_backend.h"
#include "logger.h"
#include "miso_events.h"
#include "miso_render.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdlib.h>

static MisoConfig miso__default_config(void) {
    const MisoConfig cfg = {
        .window_width = 1280,
        .window_height = 720,
        .window_title = "miso",
        .data_root = nullptr,
        .enable_vsync = true,
        .sim_tick_hz = 20,
        .max_sim_steps_per_frame = 8,
    };
    return cfg;
}

static void miso__log_library_versions(void) {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SDL version: %d.%d.%d",
                SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
                SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
                SDL_VERSIONNUM_MICRO(SDL_GetVersion()));
    if (SDL_GetVersion() < SDL_VERSION) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL version is older than the compiled version! Compiled: %d.%d.%d, "
                    "Linked: %d.%d.%d",
                    SDL_MAJOR_VERSION,
                    SDL_MINOR_VERSION,
                    SDL_MICRO_VERSION,
                    SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
                    SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
                    SDL_VERSIONNUM_MICRO(SDL_GetVersion()));
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SDL_image Version: %d.%d.%d",
                SDL_IMAGE_MAJOR_VERSION,
                SDL_IMAGE_MINOR_VERSION,
                SDL_IMAGE_MICRO_VERSION);
    if (IMG_Version() < SDL_IMAGE_VERSION) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL_image version is older than the compiled version! Compiled: "
                    "%d.%d.%d, Linked: %d.%d.%d",
                    SDL_IMAGE_MAJOR_VERSION,
                    SDL_IMAGE_MINOR_VERSION,
                    SDL_IMAGE_MICRO_VERSION,
                    SDL_VERSIONNUM_MAJOR(IMG_Version()),
                    SDL_VERSIONNUM_MINOR(IMG_Version()),
                    SDL_VERSIONNUM_MICRO(IMG_Version()));
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SDL_ttf Version: %d.%d.%d",
                SDL_TTF_MAJOR_VERSION,
                SDL_TTF_MINOR_VERSION,
                SDL_TTF_MICRO_VERSION);
    if (TTF_Version() < SDL_TTF_VERSION) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL_ttf version is older than the compiled version! Compiled: "
                    "%d.%d.%d, Linked: %d.%d.%d",
                    SDL_TTF_MAJOR_VERSION,
                    SDL_TTF_MINOR_VERSION,
                    SDL_TTF_MICRO_VERSION,
                    SDL_VERSIONNUM_MAJOR(TTF_Version()),
                    SDL_VERSIONNUM_MINOR(TTF_Version()),
                    SDL_VERSIONNUM_MICRO(TTF_Version()));
    }

    const char *base_path = SDL_GetBasePath();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL Base Path: %s", base_path ? base_path : "(null)");

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "miso version: " MISO_VERSION);
}

static bool miso__ensure_camera_capacity(MisoEngine *engine) {
    if (engine->camera_count < engine->camera_capacity) {
        return true;
    }

    const uint32_t new_capacity = engine->camera_capacity == 0 ? 4U : engine->camera_capacity * 2U;
    MisoCameraState *new_cameras = SDL_realloc(engine->cameras, sizeof(MisoCameraState) * new_capacity);
    if (!new_cameras) {
        return false;
    }

    SDL_memset(
        new_cameras + engine->camera_capacity, 0, sizeof(MisoCameraState) * (new_capacity - engine->camera_capacity));
    engine->cameras = new_cameras;
    engine->camera_capacity = new_capacity;
    return true;
}

MisoResult miso_create(const MisoConfig *cfg, MisoEngine **out_engine) {
    if (!out_engine) {
        return MISO_ERR_INVALID_ARG;
    }

    *out_engine = nullptr;

    MisoEngine *engine = SDL_calloc(1, sizeof(MisoEngine));
    if (!engine) {
        return MISO_ERR_OUT_OF_MEMORY;
    }

    LOG_init();
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
#ifdef MISO_DEBUG
    SDL_SetHint(SDL_HINT_RENDER_GPU_DEBUG, "1");
#endif
    SDL_SetAppMetadata("miso engine", MISO_VERSION, "dev.rnau.miso");
    miso__log_library_versions();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_free(engine);
        return MISO_ERR_INIT;
    }

    engine->config = cfg ? *cfg : miso__default_config();
    if (engine->config.sim_tick_hz <= 0) {
        engine->config.sim_tick_hz = 20;
    }
    if (engine->config.max_sim_steps_per_frame <= 0) {
        engine->config.max_sim_steps_per_frame = 8;
    }
    if (!engine->config.window_title) {
        engine->config.window_title = "miso";
    }
    if (miso__resolve_data_root(&engine->config, engine->data_root, sizeof(engine->data_root))) {
        engine->has_data_root = true;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "miso data root: %s", engine->data_root);
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "miso data root could not be resolved");
    }

    engine->window = SDL_CreateWindow(engine->config.window_title,
                                      engine->config.window_width,
                                      engine->config.window_height,
                                      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!engine->window) {
        SDL_Quit();
        SDL_free(engine);
        return MISO_ERR_INIT;
    }

    if (!miso__renderer_init(engine, engine->window)) {
        SDL_DestroyWindow(engine->window);
        SDL_Quit();
        SDL_free(engine);
        return MISO_ERR_GPU;
    }

    miso__renderer_set_vsync(engine->config.enable_vsync);
    miso__renderer_ui_init();

    engine->running = true;
    engine->perf_frequency = SDL_GetPerformanceFrequency();
    engine->last_counter = SDL_GetPerformanceCounter();

    if (!miso__ensure_camera_capacity(engine)) {
        miso__renderer_ui_shutdown();
        miso__renderer_shutdown();
        SDL_DestroyWindow(engine->window);
        SDL_Quit();
        SDL_free(engine);
        return MISO_ERR_OUT_OF_MEMORY;
    }

    *out_engine = engine;
    return MISO_OK;
}

void miso_destroy(MisoEngine *engine) {
    if (!engine) {
        return;
    }

    miso__renderer_ui_shutdown();
    miso__render_shutdown();
    miso__renderer_shutdown();

    if (engine->window) {
        SDL_DestroyWindow(engine->window);
    }

    SDL_free(engine->cameras);
    SDL_Quit();
    SDL_free(engine);
}

bool miso_begin_frame(MisoEngine *engine) {
    if (!engine || !engine->running) {
        return false;
    }

    const uint64_t now = SDL_GetPerformanceCounter();
    const uint64_t delta = now - engine->last_counter;
    engine->last_counter = now;

    const double dt = (double)delta / (double)engine->perf_frequency;
    engine->real_dt_seconds = (float)dt;
    engine->sim_accumulator += dt;
    return true;
}

void miso_end_frame(MisoEngine *engine) {
    if (!engine || !engine->running) {
        return;
    }

    miso__renderer_begin_frame();

    if (engine->game_registered && engine->game_hooks.on_render_world) {
        engine->game_hooks.on_render_world(engine->game_ctx, engine);
    }
    if (engine->game_registered && engine->game_hooks.on_render_ui) {
        engine->game_hooks.on_render_ui(engine->game_ctx, engine);
    }
    if (engine->game_registered && engine->game_hooks.on_render_debug) {
        engine->game_hooks.on_render_debug(engine->game_ctx, engine);
    }

    miso__renderer_end_frame();
}

void miso_get_window_size_pixels(const MisoEngine *engine, int *out_width, int *out_height) {
    if (!engine || !engine->window || !out_width || !out_height) {
        return;
    }
    SDL_GetWindowSizeInPixels(engine->window, out_width, out_height);
}

float miso_get_window_pixel_density(const MisoEngine *engine) {
    if (!engine || !engine->window) {
        return 1.0f;
    }
    return SDL_GetWindowPixelDensity(engine->window);
}

void miso_run_simulation_ticks(MisoEngine *engine, MisoSimTickFn tick_fn, void *user) {
    if (!engine || !engine->running) {
        return;
    }

    const double fixed_step = 1.0 / (double)engine->config.sim_tick_hz;
    int steps = 0;

    while (engine->sim_accumulator >= fixed_step && steps < engine->config.max_sim_steps_per_frame) {
        const float fixed_dt = (float)fixed_step;
        if (tick_fn) {
            tick_fn(user, fixed_dt);
        }
        if (engine->game_registered && engine->game_hooks.on_sim_tick) {
            engine->game_hooks.on_sim_tick(engine->game_ctx, fixed_dt);
        }

        engine->sim_accumulator -= fixed_step;
        steps++;
    }

    if (engine->sim_accumulator < 0.0) {
        engine->sim_accumulator = 0.0;
    }
}

float miso_get_real_delta_seconds(const MisoEngine *engine) {
    if (!engine) {
        return 0.0f;
    }
    return engine->real_dt_seconds;
}

float miso_get_interpolation_alpha(const MisoEngine *engine) {
    if (!engine || engine->config.sim_tick_hz <= 0) {
        return 0.0f;
    }

    const double fixed_step = 1.0 / (double)engine->config.sim_tick_hz;
    return (float)(engine->sim_accumulator / fixed_step);
}

MisoResult miso_game_register(MisoEngine *engine, const MisoGameHooks *hooks, void *game_ctx) {
    if (!engine || !hooks) {
        return MISO_ERR_INVALID_ARG;
    }

    engine->game_hooks = *hooks;
    engine->game_ctx = game_ctx;
    engine->game_registered = true;

    if (engine->game_hooks.on_reset) {
        engine->game_hooks.on_reset(engine->game_ctx);
    }

    return MISO_OK;
}

void miso__engine_request_quit(MisoEngine *engine) {
    if (!engine) {
        return;
    }
    engine->running = false;
}

MisoCameraState *miso__camera_get(MisoEngine *engine, const MisoCameraId id) {
    if (!engine || id == 0) {
        return nullptr;
    }
    const uint32_t idx = id - 1U;
    if (idx >= engine->camera_count) {
        return nullptr;
    }
    MisoCameraState *camera = &engine->cameras[idx];
    return camera->used ? camera : nullptr;
}

const MisoCameraState *miso__camera_get_const(const MisoEngine *engine, const MisoCameraId id) {
    if (!engine || id == 0) {
        return nullptr;
    }
    const uint32_t idx = id - 1U;
    if (idx >= engine->camera_count) {
        return nullptr;
    }
    const MisoCameraState *camera = &engine->cameras[idx];
    return camera->used ? camera : nullptr;
}

MisoCameraId miso_camera_create(MisoEngine *engine) {
    if (!engine) {
        return 0;
    }

    if (!miso__ensure_camera_capacity(engine)) {
        return 0;
    }

    MisoCameraState *camera = &engine->cameras[engine->camera_count];
    camera->used = true;
    camera->x = 0.0f;
    camera->y = 0.0f;
    camera->zoom = 1.0f;
    camera->pixel_snap = true;
    camera->viewport.x = 0;
    camera->viewport.y = 0;
    camera->viewport.w = engine->config.window_width;
    camera->viewport.h = engine->config.window_height;

    engine->camera_count++;
    return engine->camera_count;
}

void miso__camera_get_view_projection(const MisoEngine *const engine, const MisoCameraId id, float out_matrix[16]) {
    SDL_memset(out_matrix, 0, sizeof(float) * 16U);
    const MisoCameraState *camera = miso__camera_get_const(engine, id);
    if (!camera) {
        out_matrix[0] = 1.0f;
        out_matrix[5] = 1.0f;
        out_matrix[10] = 1.0f;
        out_matrix[15] = 1.0f;
        return;
    }

    const float scale = camera->zoom;
    const float cx = (float)camera->viewport.w * 0.5f;
    const float cy = (float)camera->viewport.h * 0.5f;

    float offx = (cx / scale) - camera->x;
    float offy = (cy / scale) - camera->y;

    if (camera->pixel_snap) {
        offx = SDL_floorf(offx);
        offy = SDL_floorf(offy);
    }

    const float w = (float)camera->viewport.w;
    const float h = (float)camera->viewport.h;

    const float m00 = 2.0f * scale / w;
    const float m11 = -2.0f * scale / h;
    const float m30 = (offx * scale * 2.0f / w) - 1.0f;
    const float m31 = 1.0f - (offy * scale * 2.0f / h);

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
    out_matrix[10] = 1.0f;
    out_matrix[11] = 0.0f;

    out_matrix[12] = m30;
    out_matrix[13] = m31;
    out_matrix[14] = 0.0f;
    out_matrix[15] = 1.0f;
}
