//
// Created by Arnau Sanz on 2/7/25.
//

#include "profiler.h"

#include "SDL3/SDL_assert.h"
#include "SDL3/SDL_timer.h"
#include "renderer/ui.h"

#include <assert.h>

static const char *const PROF_category_names[] = {[PROFILER_EVENT_HANDLING] = "event_handling",
                                                  [PROFILER_RENDER_MAP] = "render_map",
                                                  [PROFILER_RENDER_BUILDINGS] = "render_buildings",
                                                  [PROFILER_RENDER_WIREFRAMES] = "render_wireframes",
                                                  [PROFILER_RENDER_UI] = "render_UI",
                                                  [PROFILER_GPU] = "gpu_commands",
                                                  [PROFILER_WAIT_FRAME] = "wait_frame",
                                                  [PROFILER_NUKLEAR] = "nuklear",
                                                  // [PROFILER_OTHER] = "other",
                                                  [PROFILER_FRAME_TOTAL] = "frame_total"};
//check amount of profiler_type_names == ProfilerSampleTypes amount (- count)
static_assert(sizeof(PROF_category_names) / sizeof(PROF_category_names[0]) == PROFILER_CATEGORY_COUNT,
              SDL_FILE ": All profiler categories must have a declared name.");

#define TARGET_FPS 60
static constexpr float goal_frame_time = 1.0f * 1000.0f / TARGET_FPS; // 1.0f / 70.0f = 14.2857 ms per frame

#define GRAPH_COUNT 240

typedef struct ProfilerCircularBuffer {
    ProfilerSample samples[GRAPH_COUNT][PROFILER_CATEGORY_COUNT];
    float total_times[GRAPH_COUNT];
    int newest; //index of the newest sample
    int count;  //number of valid samples
} ProfilerCircularBuffer;

ProfilerCircularBuffer prof_samples = {0};
ProfilerSample measuring_samples[PROFILER_CATEGORY_COUNT] = {0};

static inline void swap_sample_buffers() {

    float total_time = 0.0f;
    for (ProfilerSampleCategory category = 0; category < PROFILER_FRAME_TOTAL; category++) {
        total_time += measuring_samples[category].duration_ms;
    }

    if (prof_samples.count > 0) {
        prof_samples.newest = (prof_samples.newest + 1) %
                              GRAPH_COUNT; //NOTE: changed this from after memset, check if it works as intended
    }

    if (prof_samples.count < GRAPH_COUNT) {
        prof_samples.count++;
    }

    SDL_memcpy(
        prof_samples.samples[prof_samples.newest], measuring_samples, sizeof(ProfilerSample) * PROFILER_CATEGORY_COUNT);
    // SDL_memset(measuring_samples, 0, sizeof(ProfilerSample)*PROFILER_CATEGORY_COUNT); //note: moved the set to 0 to PROF_frameStart() to avoid issues in frame time calculations

    prof_samples.total_times[prof_samples.newest] = total_time == 0.0f ? 1.0f : total_time; //avoid division by zero
}

typedef struct FramesPerSecond {
    float min;
    float avg;
    float max;
} FramesPerSecond_t;
static FramesPerSecond_t frames_per_second = {.min = 1e9f, .avg = 0.0f, .max = -1e9f};
static float fps_buffer[GRAPH_COUNT] = {0};

void calculate_FPS() {
    // Calculate FPS based on the last finished frame sample.
    //TODO: check if using total_times or samples[PROFILER_FRAME_TOTAL] is better.
    const float last_frame_fps = 1000.0f / prof_samples.total_times[prof_samples.newest];
    fps_buffer[prof_samples.newest] = last_frame_fps;

    if (prof_samples.newest == 0) {
        //reset FPS stats if we are at the beginning of the buffer (full lap, at least GRAPH_COUNT frames have passed)
        frames_per_second.min = last_frame_fps;
        // frames_per_second.avg = last_frame_fps;
        frames_per_second.max = last_frame_fps;

        // SDL_LogDebug(SDL_LOG_CATEGORY_TEST, "Profiler: FPS stats reset. New FPS: %.2f", last_frame_fps);
        // SDL_LogDebug(SDL_LOG_CATEGORY_TEST, "\tFormula: 1 / %.2f", prof_samples.total_times[prof_samples.newest]);
    } else {
        // Update FPS stats
        if (last_frame_fps < frames_per_second.min) {
            frames_per_second.min = last_frame_fps;
        }
        if (last_frame_fps > frames_per_second.max) {
            frames_per_second.max = last_frame_fps;
        }
    }
    frames_per_second.avg = (frames_per_second.avg * (float)(prof_samples.count - 1) + last_frame_fps) /
                            (float)(prof_samples.count); //NOTE: check
    // SDL_LogDebug(SDL_LOG_CATEGORY_TEST, "FPS stats updated. New AVG_FPS: %.2f || last: %.2f", frames_per_second.avg, last_frame_fps);
}

void PROF_frameStart() {
    if (measuring_samples[PROFILER_FRAME_TOTAL].start_time != 0) {
        //didn't do PROF_frameEnd...
        PROF_frameEnd();
    }

    SDL_memset(measuring_samples, 0, sizeof(ProfilerSample) * PROFILER_CATEGORY_COUNT);

    PROF_start(PROFILER_FRAME_TOTAL);
}

void PROF_frameEnd() {
    PROF_stop(PROFILER_FRAME_TOTAL);

    //TODO: swap sample buffers here? thinking thoughts
    swap_sample_buffers();
    calculate_FPS();
}

//starts the timer for a named section.
void PROF_start(const ProfilerSampleCategory category) {
    if (category < PROFILER_CATEGORY_COUNT) {
        measuring_samples[category].start_time = SDL_GetPerformanceCounter();
    }
}

//stops the timer and adds the duration in milliseconds.
void PROF_stop(const ProfilerSampleCategory category) {
    const Uint64 end_time = SDL_GetPerformanceCounter();
    if (category < PROFILER_CATEGORY_COUNT && measuring_samples[category].start_time > 0) {
        const Uint64 duration_ticks = end_time - measuring_samples[category].start_time;
        measuring_samples[category].duration_ms +=
            (float)(duration_ticks * 1000) / (float)SDL_GetPerformanceFrequency(); //note: check 1000
        measuring_samples[category].start_time = 0;                                //mark as stopped
    }
}

void PROF_setDuration(const ProfilerSampleCategory category, const float duration_ms) {
    if (category >= PROFILER_CATEGORY_COUNT) {
        return;
    }

    measuring_samples[category].start_time = 0;
    measuring_samples[category].duration_ms = duration_ms > 0.0f ? duration_ms : 0.0f;
}

inline float PROF_getLastFrameTime() {
    if (unlikely(prof_samples.count <= 0)) {
        return 0.0f; //no samples available
    }
    return prof_samples.total_times[prof_samples.newest]; //return the last frame time in milliseconds
}

inline float PROF_getFrameTime() {

    if (measuring_samples[PROFILER_FRAME_TOTAL].start_time == 0) {
        //already stopped, or not started
        return measuring_samples[PROFILER_FRAME_TOTAL].duration_ms; //return the last frame time in milliseconds
    }

    const Uint64 current_time = SDL_GetPerformanceCounter();
    const Uint64 elapsed_ticks = current_time - measuring_samples[PROFILER_FRAME_TOTAL].start_time;
    return (float)(elapsed_ticks * 1000) / (float)SDL_GetPerformanceFrequency();
}

float PROF_getFrameWaitTime() {
    const float elapsed_ms = PROF_getFrameTime();
    const float wait_time = goal_frame_time - elapsed_ms;
    // SDL_LogDebug(SDL_LOG_CATEGORY_TEST, "wait time: %.2f ms (elapsed: %.2f ms, goal: %.2f ms)", wait_time, elapsed_ms, goal_frame_time);
    return wait_time > 0.0f ? wait_time : 0.0f;
}

void PROF_getFPS(float *const SDL_RESTRICT min, float *const SDL_RESTRICT avg, float *const SDL_RESTRICT max) {
    *min = frames_per_second.min;
    *avg = frames_per_second.avg;
    *max = frames_per_second.max;
}

/* ------------ RENDERING ------------ */
static SDL_FColor hsv_to_fcolor(const float hue, const float saturation, const float value) {
    const float c = value * saturation;
    const float x = c * (1.0f - SDL_fabsf(SDL_fmodf(hue * 6.0f, 2.0f) - 1.0f));
    const float m = value - c;

    float r, g, b;
    if (hue < 1.0f / 6.0f) {
        r = c;
        g = x;
        b = 0;
    } else if (hue < 2.0f / 6.0f) {
        r = x;
        g = c;
        b = 0;
    } else if (hue < 3.0f / 6.0f) {
        r = 0;
        g = c;
        b = x;
    } else if (hue < 4.0f / 6.0f) {
        r = 0;
        g = x;
        b = c;
    } else if (hue < 5.0f / 6.0f) {
        r = x;
        g = 0;
        b = c;
    } else {
        r = c;
        g = 0;
        b = x;
    }

    const SDL_FColor color = {(r + m), (g + m), (b + m), 1.0f};
    return color;
}

static inline SDL_FColor fcolor_for_section(const ProfilerSampleCategory section) {
    if (section == PROFILER_FRAME_TOTAL)
        return (SDL_FColor){1.0f, 1.0f, 1.0f, 1.0f};
    const float hue = (float)section / (float)PROFILER_FRAME_TOTAL; //distribute hues evenly
    constexpr float saturation = 0.9f;                              //high saturation for vibrant colors
    constexpr float value = 0.9f;                                   //bright colors
    return hsv_to_fcolor(hue, saturation, value);
}

/* ------------ GPU Profiler State ------------ */
static MisoTextHandle title_text = 0;
static MisoTextHandle prof_category_label_texts[PROFILER_CATEGORY_COUNT] = {0};
static MisoTextHandle prof_category_value_texts[PROFILER_CATEGORY_COUNT] = {0};

void PROF_initUI(const MisoEngine *const engine, const MisoFontHandle font) {
    if (!engine || font == 0) {
        return;
    }

    if (miso_text_create(engine, font, "Debug Info", &title_text) == MISO_OK) {
        miso_text_set_background_enabled(engine, title_text, true);
        miso_text_set_background_style(engine, title_text, 0x00000099u, 0.0f);
    }

    for (int i = 0; i < PROFILER_CATEGORY_COUNT; i++) {
        char label_buffer[48] = {0};
        SDL_snprintf(label_buffer, sizeof(label_buffer), "%s:", PROF_category_names[i]);
        if (miso_text_create(engine, font, label_buffer, &prof_category_label_texts[i]) == MISO_OK) {
            miso_text_set_background_enabled(engine, prof_category_label_texts[i], true);
            miso_text_set_background_style(engine, prof_category_label_texts[i], 0x00000099u, 0.0f);
        }

        if (miso_text_create(engine, font, "", &prof_category_value_texts[i]) == MISO_OK) {
            miso_text_set_background_enabled(engine, prof_category_value_texts[i], true);
            miso_text_set_background_style(engine, prof_category_value_texts[i], 0x00000099u, 0.0f);
        }
    }
}

void PROF_deinitUI(const MisoEngine *const engine) {
    if (!engine) {
        return;
    }

    miso_text_destroy(engine, title_text);
    title_text = 0;

    for (int i = 0; i < PROFILER_CATEGORY_COUNT; i++) {
        miso_text_destroy(engine, prof_category_label_texts[i]);
        miso_text_destroy(engine, prof_category_value_texts[i]);
        prof_category_label_texts[i] = 0;
        prof_category_value_texts[i] = 0;
    }
}

void PROF_render(const MisoEngine *const engine, const SDL_FPoint position) {
    if (!engine || !miso_text_is_valid(engine, title_text))
        return;

    const int index = prof_samples.newest;
    char text_buffer[64];

    // --- Render category labels and color squares ---
    constexpr float line_height = 24.0f; // Approximate text height
    constexpr float square_size = line_height - 4.0f;
    const float text_x = position.x + (square_size * 1.5f);
    float current_y = position.y;

    miso_render_submit_ui_text_handle(engine, title_text, position.x, current_y, 0xFFFFFFFFu);
    current_y += line_height + 16.0f;

    for (ProfilerSampleCategory category = 0; category < PROFILER_CATEGORY_COUNT; category++) {
        if (category == PROFILER_FRAME_TOTAL) {
            SDL_snprintf(text_buffer,
                         sizeof(text_buffer),
                         "%6.2f | %6.2f (ms)",
                         prof_samples.samples[index][category].duration_ms,
                         goal_frame_time);
        } else {
            SDL_snprintf(
                text_buffer, sizeof(text_buffer), "%6.2f ms", prof_samples.samples[index][category].duration_ms);
        }

        const MisoTextHandle label_handle = prof_category_label_texts[category];
        const MisoTextHandle value_handle = prof_category_value_texts[category];
        if (!miso_text_is_valid(engine, label_handle) || !miso_text_is_valid(engine, value_handle)) {
            current_y += line_height + 8.0f;
            continue;
        }

        (void)miso_text_set_string(engine, value_handle, text_buffer);

        MisoTextMetrics label_metrics = {0};
        if (!miso_text_get_metrics(engine, label_handle, &label_metrics)) {
            label_metrics.width = 0.0f;
        }

        miso_render_submit_ui_text_handle(engine, label_handle, text_x, current_y - 4.0f, 0xFFFFFFFFu);
        miso_render_submit_ui_text_handle(
            engine, value_handle, text_x + label_metrics.width + 8.0f, current_y - 4.0f, 0xFFFFFFFFu);

        if (category != PROFILER_FRAME_TOTAL) {
            const SDL_FColor color = fcolor_for_section(category);
            UI_FillRect(position.x, current_y + 2.0f, square_size, square_size, color);
        }

        current_y += line_height + 8.0f;
    }

    // --- Bar chart parameters ---
    const float bar_x = position.x;
    const float bar_y = current_y + 10.0f;
    constexpr float bar_width = 12.0f;
    constexpr float bar_height = 200.0f;
    constexpr float time_graph_height = bar_height;
    const SDL_FColor outline_color = {0.0f, 0.0f, 0.0f, 1.0f};
    const SDL_FColor goal_line_color = {1.0f, 1.0f, 1.0f, 0.8f};
    const SDL_FColor time_bar_color = {1.0f, 0.6f, 0.2f, 1.0f};

    UI_FillRect(bar_x, bar_y, bar_width * GRAPH_COUNT, bar_height, outline_color);

    UI_FillRect(bar_x, bar_y + bar_height, bar_width * GRAPH_COUNT, time_graph_height, UI_COLOR_BACKGROUND_DEFAULT);

    // --- Render stacked bar chart ---
    for (int sample_idx = 0; sample_idx < prof_samples.count; sample_idx++) {
        int buf_index = (prof_samples.newest - sample_idx);
        buf_index = buf_index < 0 ? buf_index + GRAPH_COUNT : buf_index % GRAPH_COUNT;
        const double total_time = prof_samples.total_times[buf_index];
        if (total_time <= 0.0)
            continue;

        float section_y = bar_y;
        const float calculated_x = bar_x + bar_width * GRAPH_COUNT - bar_width * (float)(sample_idx + 1);

        // Draw each section of the stacked bar
        for (ProfilerSampleCategory section = 0; section < PROFILER_FRAME_TOTAL; section++) {
            const float section_height =
                (float)(prof_samples.samples[buf_index][section].duration_ms / total_time) * bar_height;
            const SDL_FColor color = fcolor_for_section(section);

            UI_FillRect(calculated_x, section_y, bar_width - 1.0f, section_height, color);
            section_y += section_height;
        }

        // Draw time graph bar below
        constexpr float scaled_height = time_graph_height / (goal_frame_time * 2.0f);
        const float time_bar_h = prof_samples.total_times[buf_index] * scaled_height;
        UI_FillRect(calculated_x, bar_y + bar_height, bar_width - 1.0f, time_bar_h, time_bar_color);
    }

    // --- Draw outlines and goal line ---
    // Bar chart outline
    UI_RectOutline(bar_x, bar_y, bar_width * GRAPH_COUNT, bar_height, outline_color, 1.0f);

    // Time graph outline
    UI_RectOutline(bar_x, bar_y + bar_height, bar_width * GRAPH_COUNT, time_graph_height, outline_color, 1.0f);

    // Goal time line (horizontal line at 50% of time graph = goal frame time)
    UI_Line(bar_x,
            bar_y + bar_height + time_graph_height * 0.5f,
            bar_x + bar_width * GRAPH_COUNT,
            bar_y + bar_height + time_graph_height * 0.5f,
            goal_line_color,
            1.0f);
}
