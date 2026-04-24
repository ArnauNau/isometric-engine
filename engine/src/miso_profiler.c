#include "miso_profiler.h"

#include "internal/miso__engine_internal.h"
#include "miso_render.h"
#include "renderer/ui.h"

#include <SDL3/SDL.h>

#define MISO_PROFILER_TARGET_FPS 60

typedef struct MisoProfilerSample {
    uint64_t start_time;
    float duration_ms;
} MisoProfilerSample;

typedef struct MisoProfilerHistory {
    MisoProfilerSample samples[MISO_PROFILER_HISTORY_COUNT][MISO_PROFILER_CATEGORY_MAX];
    float total_times[MISO_PROFILER_HISTORY_COUNT];
    int newest;
    int count;
} MisoProfilerHistory;

struct MisoProfilerState {
    MisoProfilerCategoryInfo categories[MISO_PROFILER_CATEGORY_MAX];
    MisoProfilerSample measuring[MISO_PROFILER_CATEGORY_MAX];
    MisoProfilerHistory history;
    int category_count;
    uint64_t perf_frequency;
    float goal_frame_time_ms;
    float fps_min;
    float fps_avg;
    float fps_max;
    MisoTextHandle overlay_title_text;
    MisoTextHandle overlay_label_texts[MISO_PROFILER_CATEGORY_MAX];
    MisoTextHandle overlay_value_texts[MISO_PROFILER_CATEGORY_MAX];
};

static const MisoProfilerCategoryInfo miso__engine_categories[MISO_PROFILER_ENGINE_CATEGORY_COUNT] = {
    [MISO_PROFILER_ENGINE_EVENTS] = {.active = true,
                                     .layer = MISO_PROFILER_LAYER_ENGINE,
                                     .parent = MISO_PROFILER_ENGINE_FRAME_TOTAL,
                                     .name = "engine.events",
                                     .rgba8 = 0xE65A5AFFu},
    [MISO_PROFILER_ENGINE_FIXED_TICKS] = {.active = true,
                                          .layer = MISO_PROFILER_LAYER_ENGINE,
                                          .parent = MISO_PROFILER_ENGINE_FRAME_TOTAL,
                                          .name = "engine.fixed_ticks",
                                          .rgba8 = 0xE6A85AFFu},
    [MISO_PROFILER_ENGINE_RENDER_WORLD] = {.active = true,
                                           .layer = MISO_PROFILER_LAYER_ENGINE,
                                           .parent = MISO_PROFILER_ENGINE_FRAME_TOTAL,
                                           .name = "engine.render_world",
                                           .rgba8 = 0xD8E65AFFu},
    [MISO_PROFILER_ENGINE_RENDER_UI] = {.active = true,
                                        .layer = MISO_PROFILER_LAYER_ENGINE,
                                        .parent = MISO_PROFILER_ENGINE_FRAME_TOTAL,
                                        .name = "engine.render_ui",
                                        .rgba8 = 0x78E65AFFu},
    [MISO_PROFILER_ENGINE_RENDER_DEBUG] = {.active = true,
                                           .layer = MISO_PROFILER_LAYER_ENGINE,
                                           .parent = MISO_PROFILER_ENGINE_FRAME_TOTAL,
                                           .name = "engine.render_debug",
                                           .rgba8 = 0x5AE6C8FFu},
    [MISO_PROFILER_ENGINE_RENDERER_ACQUIRE] = {.active = true,
                                               .layer = MISO_PROFILER_LAYER_ENGINE,
                                               .parent = MISO_PROFILER_ENGINE_FRAME_TOTAL,
                                               .name = "renderer.acquire",
                                               .rgba8 = 0x5AA8E6FFu},
    [MISO_PROFILER_ENGINE_RENDERER_RECORD] = {.active = true,
                                              .layer = MISO_PROFILER_LAYER_ENGINE,
                                              .parent = MISO_PROFILER_ENGINE_FRAME_TOTAL,
                                              .name = "renderer.record",
                                              .rgba8 = 0x5A62E6FFu},
    [MISO_PROFILER_ENGINE_RENDERER_SUBMIT] = {.active = true,
                                              .layer = MISO_PROFILER_LAYER_ENGINE,
                                              .parent = MISO_PROFILER_ENGINE_FRAME_TOTAL,
                                              .name = "renderer.submit",
                                              .rgba8 = 0xA85AE6FFu},
    [MISO_PROFILER_ENGINE_DEBUG_UI] = {.active = true,
                                       .layer = MISO_PROFILER_LAYER_ENGINE,
                                       .parent = MISO_PROFILER_ENGINE_RENDER_DEBUG,
                                       .name = "engine.debug_ui",
                                       .rgba8 = 0xE65AC8FFu},
    [MISO_PROFILER_ENGINE_FRAME_TOTAL] = {.active = true,
                                          .layer = MISO_PROFILER_LAYER_ENGINE,
                                          .parent = MISO_PROFILER_CATEGORY_NONE,
                                          .name = "engine.frame_total",
                                          .rgba8 = 0xFFFFFFFFu},
};

static bool miso__profiler_valid_category(const MisoProfilerState *const profiler,
                                          const MisoProfilerCategoryId category) {
    return profiler && category < (MisoProfilerCategoryId)profiler->category_count &&
           profiler->categories[category].active;
}

static SDL_FColor miso__profiler_fcolor_from_rgba8(const uint32_t rgba8) {
    return (SDL_FColor){
        .r = (float)((rgba8 >> 24) & 0xFFU) / 255.0f,
        .g = (float)((rgba8 >> 16) & 0xFFU) / 255.0f,
        .b = (float)((rgba8 >> 8) & 0xFFU) / 255.0f,
        .a = (float)(rgba8 & 0xFFU) / 255.0f,
    };
}

static float
miso__profiler_elapsed_ms(const MisoProfilerState *const profiler, const uint64_t start, const uint64_t end) {
    if (!profiler || profiler->perf_frequency == 0U || end < start) {
        return 0.0f;
    }
    return (float)((double)(end - start) * 1000.0 / (double)profiler->perf_frequency);
}

static void miso__profiler_swap_sample_buffers(MisoProfilerState *const profiler) {
    float total_time = 0.0f;
    for (int category = 0; category < profiler->category_count; category++) {
        if (category == MISO_PROFILER_ENGINE_FRAME_TOTAL) {
            continue;
        }
        total_time += profiler->measuring[category].duration_ms;
    }

    if (profiler->history.count > 0) {
        profiler->history.newest = (profiler->history.newest + 1) % MISO_PROFILER_HISTORY_COUNT;
    }
    if (profiler->history.count < MISO_PROFILER_HISTORY_COUNT) {
        profiler->history.count++;
    }

    SDL_memcpy(profiler->history.samples[profiler->history.newest],
               profiler->measuring,
               sizeof(MisoProfilerSample) * MISO_PROFILER_CATEGORY_MAX);
    profiler->history.total_times[profiler->history.newest] = total_time > 0.0f ? total_time : 1.0f;
}

static void miso__profiler_calculate_fps(MisoProfilerState *const profiler) {
    const float total_ms =
        profiler->history.samples[profiler->history.newest][MISO_PROFILER_ENGINE_FRAME_TOTAL].duration_ms;
    const float frame_ms = total_ms > 0.0f ? total_ms : profiler->history.total_times[profiler->history.newest];
    const float fps = frame_ms > 0.0f ? 1000.0f / frame_ms : 0.0f;

    if (profiler->history.count <= 1 || profiler->history.newest == 0) {
        profiler->fps_min = fps;
        profiler->fps_max = fps;
        profiler->fps_avg = fps;
        return;
    }

    if (fps < profiler->fps_min) {
        profiler->fps_min = fps;
    }
    if (fps > profiler->fps_max) {
        profiler->fps_max = fps;
    }
    profiler->fps_avg =
        (profiler->fps_avg * (((float)profiler->history.count / 2) - 1) + fps) / (float)profiler->history.count / 2;
}

MisoResult miso_profiler_init(MisoEngine *const engine) {
    if (!engine) {
        return MISO_ERR_INVALID_ARG;
    }
    if (engine->profiler) {
        return MISO_OK;
    }

    MisoProfilerState *const profiler = SDL_calloc(1, sizeof(MisoProfilerState));
    if (!profiler) {
        return MISO_ERR_OUT_OF_MEMORY;
    }

    profiler->perf_frequency = SDL_GetPerformanceFrequency();
    profiler->goal_frame_time_ms = 1000.0f / (float)MISO_PROFILER_TARGET_FPS;
    profiler->fps_min = 1e9f;
    profiler->fps_avg = 0.0f;
    profiler->fps_max = 0.0f;
    SDL_memcpy(profiler->categories, miso__engine_categories, sizeof(miso__engine_categories));
    profiler->category_count = MISO_PROFILER_ENGINE_CATEGORY_COUNT;
    engine->profiler = profiler;
    return MISO_OK;
}

void miso_profiler_shutdown(MisoEngine *const engine) {
    if (!engine || !engine->profiler) {
        return;
    }
    miso_profiler_overlay_shutdown(engine);
    SDL_free(engine->profiler);
    engine->profiler = nullptr;
}

void miso_profiler_frame_start(MisoEngine *const engine) {
    MisoProfilerState *const profiler = engine ? engine->profiler : nullptr;
    if (!profiler) {
        return;
    }
    if (profiler->measuring[MISO_PROFILER_ENGINE_FRAME_TOTAL].start_time != 0U) {
        miso_profiler_frame_end(engine);
    }
    SDL_memset(profiler->measuring, 0, sizeof(profiler->measuring));
    miso_profiler_begin(engine, MISO_PROFILER_ENGINE_FRAME_TOTAL);
}

void miso_profiler_frame_end(MisoEngine *const engine) {
    MisoProfilerState *const profiler = engine ? engine->profiler : nullptr;
    if (!profiler) {
        return;
    }
    miso_profiler_end(engine, MISO_PROFILER_ENGINE_FRAME_TOTAL);
    miso__profiler_swap_sample_buffers(profiler);
    miso__profiler_calculate_fps(profiler);
}

void miso_profiler_begin(MisoEngine *const engine, const MisoProfilerCategoryId category) {
    MisoProfilerState *const profiler = engine ? engine->profiler : nullptr;
    if (!miso__profiler_valid_category(profiler, category)) {
        return;
    }
    profiler->measuring[category].start_time = SDL_GetPerformanceCounter();
}

void miso_profiler_end(MisoEngine *const engine, const MisoProfilerCategoryId category) {
    MisoProfilerState *const profiler = engine ? engine->profiler : nullptr;
    if (!miso__profiler_valid_category(profiler, category) || profiler->measuring[category].start_time == 0U) {
        return;
    }
    const uint64_t end = SDL_GetPerformanceCounter();
    profiler->measuring[category].duration_ms +=
        miso__profiler_elapsed_ms(profiler, profiler->measuring[category].start_time, end);
    profiler->measuring[category].start_time = 0U;
}

void miso_profiler_set_duration(MisoEngine *const engine,
                                const MisoProfilerCategoryId category,
                                const float duration_ms) {
    MisoProfilerState *const profiler = engine ? engine->profiler : nullptr;
    if (!miso__profiler_valid_category(profiler, category)) {
        return;
    }
    profiler->measuring[category].start_time = 0U;
    profiler->measuring[category].duration_ms = duration_ms > 0.0f ? duration_ms : 0.0f;
}

bool miso_profiler_register_game_category(MisoEngine *const engine,
                                          const char *const name,
                                          const uint32_t rgba8,
                                          MisoProfilerCategoryId *const out_category) {
    return miso_profiler_register_game_category_child(
        engine, MISO_PROFILER_ENGINE_FRAME_TOTAL, name, rgba8, out_category);
}

bool miso_profiler_register_game_category_child(MisoEngine *const engine,
                                                const MisoProfilerCategoryId parent,
                                                const char *const name,
                                                const uint32_t rgba8,
                                                MisoProfilerCategoryId *const out_category) {
    MisoProfilerState *const profiler = engine ? engine->profiler : nullptr;
    if (!profiler || !name || !out_category || profiler->category_count >= MISO_PROFILER_CATEGORY_MAX) {
        return false;
    }
    if (parent != MISO_PROFILER_CATEGORY_NONE && !miso__profiler_valid_category(profiler, parent)) {
        return false;
    }

    const int category = profiler->category_count++;
    profiler->categories[category] = (MisoProfilerCategoryInfo){
        .active = true,
        .layer = MISO_PROFILER_LAYER_GAME,
        .parent = parent,
        .rgba8 = rgba8,
    };
    SDL_strlcpy(profiler->categories[category].name, name, sizeof(profiler->categories[category].name));
    *out_category = (MisoProfilerCategoryId)category;
    return true;
}

bool miso_profiler_get_snapshot(const MisoEngine *const engine, MisoProfilerSnapshot *const out_snapshot) {
    const MisoProfilerState *const profiler = engine ? engine->profiler : nullptr;
    if (!profiler || !out_snapshot) {
        return false;
    }

    SDL_memset(out_snapshot, 0, sizeof(*out_snapshot));
    SDL_memcpy(out_snapshot->categories, profiler->categories, sizeof(profiler->categories));
    out_snapshot->category_count = profiler->category_count;
    out_snapshot->newest = profiler->history.newest;
    out_snapshot->count = profiler->history.count;
    out_snapshot->goal_frame_time_ms = profiler->goal_frame_time_ms;
    out_snapshot->fps_min = profiler->fps_min;
    out_snapshot->fps_avg = profiler->fps_avg;
    out_snapshot->fps_max = profiler->fps_max;

    for (int frame = 0; frame < profiler->history.count && frame < MISO_PROFILER_HISTORY_COUNT; frame++) {
        const float measured_total = profiler->history.samples[frame][MISO_PROFILER_ENGINE_FRAME_TOTAL].duration_ms;
        out_snapshot->frames[frame].total_duration_ms =
            measured_total > 0.0f ? measured_total : profiler->history.total_times[frame];
        for (int category = 0; category < profiler->category_count; category++) {
            out_snapshot->frames[frame].category_duration_ms[category] =
                profiler->history.samples[frame][category].duration_ms;
        }
    }
    return true;
}

const char *miso_profiler_get_category_name(const MisoEngine *const engine, const MisoProfilerCategoryId category) {
    const MisoProfilerState *const profiler = engine ? engine->profiler : nullptr;
    if (!miso__profiler_valid_category(profiler, category)) {
        return "unknown";
    }
    return profiler->categories[category].name;
}

uint32_t miso_profiler_get_category_color(const MisoEngine *const engine, const MisoProfilerCategoryId category) {
    const MisoProfilerState *const profiler = engine ? engine->profiler : nullptr;
    if (!miso__profiler_valid_category(profiler, category)) {
        return 0xFFFFFFFFu;
    }
    return profiler->categories[category].rgba8;
}

void miso_profiler_get_fps(const MisoEngine *const engine,
                           float *const out_min,
                           float *const out_avg,
                           float *const out_max) {
    const MisoProfilerState *const profiler = engine ? engine->profiler : nullptr;
    if (!profiler) {
        if (out_min) {
            *out_min = 0.0f;
        }
        if (out_avg) {
            *out_avg = 0.0f;
        }
        if (out_max) {
            *out_max = 0.0f;
        }
        return;
    }
    if (out_min) {
        *out_min = profiler->fps_min;
    }
    if (out_avg) {
        *out_avg = profiler->fps_avg;
    }
    if (out_max) {
        *out_max = profiler->fps_max;
    }
}

float miso_profiler_get_last_frame_time_ms(const MisoEngine *const engine) {
    const MisoProfilerState *const profiler = engine ? engine->profiler : nullptr;
    if (!profiler || profiler->history.count <= 0) {
        return 0.0f;
    }
    const int frame = profiler->history.newest;
    const float measured_total = profiler->history.samples[frame][MISO_PROFILER_ENGINE_FRAME_TOTAL].duration_ms;
    return measured_total > 0.0f ? measured_total : profiler->history.total_times[frame];
}

void miso_profiler_overlay_init(MisoEngine *const engine, const MisoFontHandle font) {
    MisoProfilerState *const profiler = engine ? engine->profiler : nullptr;
    if (!profiler || font == 0) {
        return;
    }
    if (miso_text_create(engine, font, "Profiler", &profiler->overlay_title_text) == MISO_OK) {
        miso_text_set_background_enabled(engine, profiler->overlay_title_text, true);
        miso_text_set_background_style(engine, profiler->overlay_title_text, 0x00000099u, 0.0f);
    }
    for (int i = 0; i < profiler->category_count; i++) {
        char label[64] = {0};
        SDL_snprintf(label, sizeof(label), "%s:", profiler->categories[i].name);
        if (miso_text_create(engine, font, label, &profiler->overlay_label_texts[i]) == MISO_OK) {
            miso_text_set_background_enabled(engine, profiler->overlay_label_texts[i], true);
            miso_text_set_background_style(engine, profiler->overlay_label_texts[i], 0x00000099u, 0.0f);
        }
        if (miso_text_create(engine, font, "", &profiler->overlay_value_texts[i]) == MISO_OK) {
            miso_text_set_background_enabled(engine, profiler->overlay_value_texts[i], true);
            miso_text_set_background_style(engine, profiler->overlay_value_texts[i], 0x00000099u, 0.0f);
        }
    }
}

void miso_profiler_overlay_shutdown(MisoEngine *const engine) {
    MisoProfilerState *const profiler = engine ? engine->profiler : nullptr;
    if (!profiler) {
        return;
    }
    miso_text_destroy(engine, profiler->overlay_title_text);
    profiler->overlay_title_text = 0;
    for (int i = 0; i < MISO_PROFILER_CATEGORY_MAX; i++) {
        miso_text_destroy(engine, profiler->overlay_label_texts[i]);
        miso_text_destroy(engine, profiler->overlay_value_texts[i]);
        profiler->overlay_label_texts[i] = 0;
        profiler->overlay_value_texts[i] = 0;
    }
}

void miso_profiler_overlay_render(MisoEngine *const engine, const SDL_FPoint position) {
    MisoProfilerState *const profiler = engine ? engine->profiler : nullptr;
    if (!profiler || profiler->history.count <= 0 || !miso_text_is_valid(engine, profiler->overlay_title_text)) {
        return;
    }

    const int frame = profiler->history.newest;
    char text[64] = {0};
    const float line_height = 24.0f;
    const float square_size = line_height - 4.0f;
    const float text_x = position.x + square_size * 1.5f;
    float y = position.y;

    miso_render_submit_ui_text_handle(engine, profiler->overlay_title_text, position.x, y, 0xFFFFFFFFu);
    y += line_height + 16.0f;

    for (int category = 0; category < profiler->category_count; category++) {
        const MisoTextHandle label_handle = profiler->overlay_label_texts[category];
        const MisoTextHandle value_handle = profiler->overlay_value_texts[category];
        if (!miso_text_is_valid(engine, label_handle) || !miso_text_is_valid(engine, value_handle)) {
            y += line_height + 8.0f;
            continue;
        }

        if (category == MISO_PROFILER_ENGINE_FRAME_TOTAL) {
            SDL_snprintf(text,
                         sizeof(text),
                         "%6.2f | %6.2f ms",
                         profiler->history.samples[frame][category].duration_ms,
                         profiler->goal_frame_time_ms);
        } else {
            SDL_snprintf(text, sizeof(text), "%6.2f ms", profiler->history.samples[frame][category].duration_ms);
        }
        (void)miso_text_set_string(engine, value_handle, text);

        MisoTextMetrics metrics = {0};
        if (!miso_text_get_metrics(engine, label_handle, &metrics)) {
            metrics.width = 0.0f;
        }
        miso_render_submit_ui_text_handle(engine, label_handle, text_x, y - 4.0f, 0xFFFFFFFFu);
        miso_render_submit_ui_text_handle(engine, value_handle, text_x + metrics.width + 8.0f, y - 4.0f, 0xFFFFFFFFu);
        if (category != MISO_PROFILER_ENGINE_FRAME_TOTAL) {
            UI_FillRect(position.x,
                        y + 2.0f,
                        square_size,
                        square_size,
                        miso__profiler_fcolor_from_rgba8(profiler->categories[category].rgba8));
        }
        y += line_height + 8.0f;
    }
}
