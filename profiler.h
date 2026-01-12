//
// Created by Arnau Sanz on 2/7/25.
//

#ifndef PROFILER_H
#define PROFILER_H

#include "SDL3/SDL_stdinc.h"
#include "SDL3_ttf/SDL_ttf.h"

typedef enum ProfilerSampleCategory {
    PROFILER_EVENT_HANDLING,
    PROFILER_RENDER_MAP,
    PROFILER_RENDER_BUILDINGS,
    PROFILER_RENDER_WIREFRAMES,
    PROFILER_RENDER_UI,
    PROFILER_GPU,
    PROFILER_WAIT_FRAME,
    PROFILER_NUKLEAR,
    // PROFILER_OTHER,
    PROFILER_FRAME_TOTAL,
    PROFILER_CATEGORY_COUNT //enum counter
} ProfilerSampleCategory;


typedef struct ProfilerSample {
    Uint64 start_time;
    float duration_ms;
} ProfilerSample;

#define unlikely(x)       __builtin_expect(!!(x), 0)
#define likely(x)         __builtin_expect(!!(x), 1)
/**
 * Must be called ONCE per frame loop, BEFORE any/all PROF_start() calls, ideally at the beginning of the frame loop, or right after the PROF_frameEnd() call.
 * It starts counting the total frame time for the frame sample.
 */
void PROF_frameStart();

/**
 * Should be called ONCE per frame loop, AFTER all PROF_stop() calls.
 * It stops counting the total frame time for the frame sample.
 *
 * If it's not called in the frame loop, PROF_frameStart() will also act as the PROF_frameEnd() of the previous frame sample.
 */
void PROF_frameEnd();

/**
 * Starts (or resumes) counting time for a specific ProfilerSampleCategory.
 * @param category profiler category to measure.
 * @enum ProfilerSampleCategory
 */
void PROF_start(ProfilerSampleCategory category);

/**
 * Stops counting time for a specific ProfilerSampleCategory.
 * @param category profiler category to stop measuring.
 * @enum ProfilerSampleCategory
 */
void PROF_stop(ProfilerSampleCategory category);


/**
 * Calculates the total time in milliseconds for the current frame sample.
 * @return total time in milliseconds for the current frame sample.
 * @note This function should be called after PROF_frameEnd() to get the correct total time.
 * @note If PROF_frameEnd() is not called, it will return the time since the last PROF_frameStart() call.
 */
float PROF_getFrameTime();
/**
 * Returns the total time in milliseconds for the last frame sample.
 * @return total time in milliseconds for the last frame sample.
 * @note If PROF_frameEnd() is not called in the whole frame loop, there's no way of getting the *last* frame time.
 * @note also known as deltaTime.
 * //TODO: rename to PROF_getDeltaTime() or something similar. Depending on use, milliseconds might not be precise enough, but I can't think of what use that might be.
 */
float PROF_getLastFrameTime();

/**
 * Calculates the total time in milliseconds to wait for the next frame, as to achieve the target FPS.
 * @note This function should be called after PROF_frameEnd() to get the correct wait time.
 * @note If PROF_frameEnd() is not called, it will return the time since the last PROF_frameStart() call.
 * @note The wait time is calculated based on the target FPS defined in the engine/settings.
 * @return Wait time in milliseconds for the next frame.
 */
float PROF_getFrameWaitTime();

/**
 * Gets the minimum, average and maximum FPS for the most recently finished frame sample.
 * @param min pointer to store the minimum FPS.
 * @param avg pointer to store the average FPS.
 * @param max pointer to store the maximum FPS.
 * @note This function returns information regarding finished samples. If called during a frame sample, it will not account for the current frame (as it needs complete frame times to give correct information).
 */
void PROF_getFPS(float *SDL_RESTRICT min, float *SDL_RESTRICT avg, float *SDL_RESTRICT max);

/* ------------ rendering the profiler ------------ */
/**
 * Initialize the GPU profiler rendering resources.
 * Must be called once after the renderer is initialized.
 * @param engine The TTF text engine from Renderer_GetTextEngine()
 * @param font The font to use for profiler text
 */
void PROF_initUI(TTF_TextEngine *engine, TTF_Font *font);

/**
 * Shutdown and free GPU profiler rendering resources.
 * Should be called before renderer shutdown.
 */
void PROF_deinitUI(void);

/**
 * Render the profiler using the GPU renderer.
 * Must be called between Renderer_BeginFrame() and Renderer_EndFrame().
 * @param position Top-left position for the profiler display
 */
void PROF_render(SDL_FPoint position);

#endif //PROFILER_H
