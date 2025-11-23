//
// Created by Arnau Sanz on 2/7/25.
//

#include "profiler.h"

#include "engine.h"
#include "SDL3/SDL_timer.h"

static const char *const PROF_category_names[] = {
    [PROFILER_EVENT_HANDLING] = "event_handling",
    [PROFILER_RENDER_MAP] = "render_map",
    [PROFILER_RENDER_BUILDINGS] = "render_buildings",
    [PROFILER_RENDER_WIREFRAMES] = "render_wireframes",
    [PROFILER_RENDER_UI] = "render_UI",
    [PROFILER_GPU] = "gpu_commands",
    [PROFILER_WAIT_FRAME] = "wait_frame",
    [PROFILER_GL] = "openGL",
    // [PROFILER_OTHER] = "other",
    [PROFILER_FRAME_TOTAL] = "frame_total"
};
//check amount of profiler_type_names == ProfilerSampleTypes amount (- count)
static_assert(sizeof(PROF_category_names) / sizeof(PROF_category_names[0]) == PROFILER_CATEGORY_COUNT, __ASSERT_FILE_NAME ": All profiler categories must have a declared name." );


#define MAX_FRAMES 60
static constexpr float goal_frame_time = 1.0f * 1000.0f / MAX_FRAMES; // 1.0f / 70.0f = 14.2857 ms per frame

#define GRAPH_COUNT 60

typedef struct ProfilerCircularBuffer {
    ProfilerSample samples[GRAPH_COUNT][PROFILER_CATEGORY_COUNT];
    float total_times[GRAPH_COUNT];
    int newest;  //index of the newest sample
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
        prof_samples.newest = (prof_samples.newest+1) % GRAPH_COUNT; //NOTE: changed this from after memset, check if it works as intended
    }

    if (prof_samples.count < GRAPH_COUNT) {
        prof_samples.count++;
    }

    SDL_memcpy(prof_samples.samples[prof_samples.newest], measuring_samples, sizeof(ProfilerSample)*PROFILER_CATEGORY_COUNT);
    // SDL_memset(measuring_samples, 0, sizeof(ProfilerSample)*PROFILER_CATEGORY_COUNT); //note: moved the set to 0 to PROF_frameStart() to avoid issues in frame time calculations

    prof_samples.total_times[prof_samples.newest] = total_time == 0.0f ? 1.0f : total_time; //avoid division by zero
}

typedef struct FramesPerSecond {
    float min;
    float avg;
    float max;
} FramesPerSecond_t;
static FramesPerSecond_t frames_per_second = {.min = 1e9f, .avg = 0.0f, .max = -1e9f};
static float fps_buffer[MAX_FRAMES] = {0};

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
    frames_per_second.avg = (frames_per_second.avg * (float)(prof_samples.count-1) + last_frame_fps) / (float)(prof_samples.count); //NOTE: check
    // SDL_LogDebug(SDL_LOG_CATEGORY_TEST, "FPS stats updated. New AVG_FPS: %.2f || last: %.2f", frames_per_second.avg, last_frame_fps);
}

void PROF_frameStart() {
    if (measuring_samples[PROFILER_FRAME_TOTAL].start_time != 0) {
        //didn't do PROF_frameEnd...
        PROF_frameEnd();
    }

    SDL_memset(measuring_samples, 0, sizeof(ProfilerSample)*PROFILER_CATEGORY_COUNT);

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
        measuring_samples[category].duration_ms += (float)(duration_ticks * 1000) / (float)SDL_GetPerformanceFrequency();//note: check 1000
        measuring_samples[category].start_time = 0; //mark as stopped
    }
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
static SDL_Color hsv_to_rgb(const float hue, const float saturation, const float value) {
    const float c = value * saturation;
    const float x = c * (1.0f - SDL_fabsf(SDL_fmodf(hue * 6.0f, 2.0f) - 1.0f));
    const float m = value - c;

    float r, g, b;
    if (hue < 1.0f / 6.0f) {
        r = c; g = x; b = 0;
    } else if (hue < 2.0f / 6.0f) {
        r = x; g = c; b = 0;
    } else if (hue < 3.0f / 6.0f) {
        r = 0; g = c; b = x;
    } else if (hue < 4.0f / 6.0f) {
        r = 0; g = x; b = c;
    } else if (hue < 5.0f / 6.0f) {
        r = x; g = 0; b = c;
    } else {
        r = c; g = 0; b = x;
    }

    const SDL_Color color = {
        (Uint8)((r + m) * 255),
        (Uint8)((g + m) * 255),
        (Uint8)((b + m) * 255),
        255
    };
    return color;
}

static inline SDL_Color color_for_section(const ProfilerSampleCategory section) {
    if (section == PROFILER_FRAME_TOTAL) return (SDL_Color){255, 255, 255, 255};
    const float hue = (float)section / (float)PROFILER_FRAME_TOTAL; //distribute hues evenly
    constexpr float saturation = 0.9f; //high saturation for vibrant colors
    constexpr float value = 0.9f; //bright colors
    return hsv_to_rgb(hue, saturation, value);
}

static void render_profiler_bar(SDL_Renderer *const SDL_RESTRICT renderer, const float x, const float y, const float width, const float height) {

    for (int sample_idx = 0; sample_idx < prof_samples.count; sample_idx++) {
        int index = (prof_samples.newest - sample_idx);
        index = index < 0 ? index + GRAPH_COUNT : index % GRAPH_COUNT; //handle negative index wrap-around (what a proper modulo would do)
        const double total_time = prof_samples.total_times[index];

        float current_y = y;
        const float end_start_x = x + width * GRAPH_COUNT;
        const float calculated_x = end_start_x - width * (float)(sample_idx+1);
        //drawing each section of the bar
        for (ProfilerSampleCategory section_idx = 0; section_idx < PROFILER_FRAME_TOTAL; section_idx++) {
            const float section_height = (float)(prof_samples.samples[index][section_idx].duration_ms / total_time) * height;

            //unique color for each section
            const SDL_Color color = color_for_section(section_idx);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);

            SDL_FRect section_rect = {calculated_x, current_y, width, section_height};
            SDL_RenderFillRect(renderer, &section_rect);

            current_y += section_height; //move to the next section
        }

        //total time graph
        const float scaled_height = (float)height / (goal_frame_time * 2.0f);

        SDL_SetRenderDrawColor(renderer, 255, 150, 50, 255);
        SDL_RenderFillRect(renderer, &(SDL_FRect){calculated_x, y+height, width, prof_samples.total_times[index] * scaled_height});
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    }

    //time graph borders + goal time line
    SDL_RenderLine(renderer, x, y+height+height/2.0f, x+width*GRAPH_COUNT, y+height+height/2.0f);  //TODO: change to actual goal time height line!!
    SDL_RenderRect(renderer, &(SDL_FRect){x, y+height, width*GRAPH_COUNT, height});

    //profiler graph borders
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderRect(renderer, &(SDL_FRect){x, y, width*GRAPH_COUNT, height});
}

void PROF_render(SDL_Renderer *const SDL_RESTRICT renderer, TTF_Font *const SDL_RESTRICT font, SDL_FPoint position) {
    char profiler_text[64];
    const int index = prof_samples.newest;
    //calculate total frame time

    for (ProfilerSampleCategory category = 0; category < PROFILER_CATEGORY_COUNT; category++) {
        if (category == PROFILER_FRAME_TOTAL) {
            snprintf(profiler_text, sizeof(profiler_text), " %s: %.2f | %.2f (ms)",
                     PROF_category_names[category], prof_samples.samples[index][category].duration_ms, goal_frame_time);
        } else {
            snprintf(profiler_text, sizeof(profiler_text), " %s: %2.2f ms",
                     PROF_category_names[category], prof_samples.samples[index][category].duration_ms);
        }
        SDL_Surface *const prof_surface = TTF_RenderText_LCD(font, profiler_text, strlen(profiler_text), (SDL_Color){255, 255, 255, 255}, (SDL_Color){0, 0, 0, 125});
        if (prof_surface) {
            SDL_Texture *const prof_texture = SDL_CreateTextureFromSurface(renderer, prof_surface);
            if (likely(prof_texture)) {
                const SDL_FRect text_rect_ = {position.x+(float)prof_surface->h, position.y, (float)prof_surface->w, (float)prof_surface->h};
                SDL_RenderTexture(renderer, prof_texture, nullptr, &text_rect_);
                SDL_DestroyTexture(prof_texture);

                if (category != PROFILER_FRAME_TOTAL) {
                    const SDL_Color color = color_for_section(category);
                    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
                    SDL_RenderFillRect(renderer, &(SDL_FRect){text_rect_.x-text_rect_.h, text_rect_.y, text_rect_.h, text_rect_.h});
                }
            }
            position.y += (float)prof_surface->h + 5.0f; //move down for the next line
            SDL_DestroySurface(prof_surface);
        }
    }
    // swap_sample_buffers();
    render_profiler_bar(renderer, position.x, position.y, 20.0f, 400.0f);
}
