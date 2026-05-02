#ifndef MISO_PROFILER_H
#define MISO_PROFILER_H

#include "miso_engine.h"
#include "miso_text.h"

#define MISO_PROFILER_HISTORY_COUNT 240
#define MISO_PROFILER_CATEGORY_MAX 64
#define MISO_PROFILER_CATEGORY_NAME_MAX 53
#define MISO_PROFILER_CATEGORY_NONE UINT16_MAX

typedef uint16_t MisoProfilerCategoryId;

typedef enum MisoProfilerLayer : uint8_t { MISO_PROFILER_LAYER_ENGINE = 0, MISO_PROFILER_LAYER_GAME } MisoProfilerLayer;

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
    char name[MISO_PROFILER_CATEGORY_NAME_MAX];
    bool active;
    MisoProfilerLayer layer;
    MisoProfilerCategoryId parent;
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

/**
 * Initializes profiler state on an engine.
 *
 * miso_create() calls this automatically. Calling it again on an already
 * initialized engine is harmless and returns MISO_OK.
 *
 * \param engine Engine that owns profiler state.
 * \return MISO_OK on success or an error code.
 */
MisoResult miso_profiler_init(MisoEngine *engine);

/**
 * Shuts down profiler state and any profiler overlay text objects.
 *
 * Passing an engine with no profiler state is allowed.
 *
 * \param engine Engine whose profiler should shut down.
 */
void miso_profiler_shutdown(MisoEngine *engine);

/**
 * Starts a new profiler frame and begins measuring frame total time.
 *
 * If a previous frame total is still open, it is ended first. Normal engine
 * users do not need to call this directly because miso_begin_frame() does it.
 *
 * \param engine Engine whose profiler should start a frame.
 */
void miso_profiler_frame_start(const MisoEngine *engine);

/**
 * Ends frame total measurement and stores the frame in the rolling history.
 *
 * Normal engine users do not need to call this directly because
 * miso_end_frame() does it.
 *
 * \param engine Engine whose profiler should end a frame.
 */
void miso_profiler_frame_end(const MisoEngine *engine);

/**
 * Begins measuring a profiler category.
 *
 * Invalid or inactive categories are ignored. Re-entering the same category
 * before ending it overwrites the pending start time.
 *
 * \param engine Engine whose profiler is active.
 * \param category Category id to begin measuring.
 */
void miso_profiler_begin(const MisoEngine *engine, MisoProfilerCategoryId category);

/**
 * Ends measuring a profiler category and accumulates elapsed milliseconds.
 *
 * Calls with no matching begin, invalid categories, or inactive categories are
 * ignored.
 *
 * \param engine Engine whose profiler is active.
 * \param category Category id to end measuring.
 */
void miso_profiler_end(const MisoEngine *engine, MisoProfilerCategoryId category);

/**
 * Sets a category duration explicitly for the current frame.
 *
 * This is used for renderer timings measured outside begin/end pairs. Negative
 * durations are clamped to 0.
 *
 * \param engine Engine whose profiler is active.
 * \param category Category id to update.
 * \param duration_ms Duration in milliseconds.
 */
void miso_profiler_set_duration(const MisoEngine *engine, MisoProfilerCategoryId category, float duration_ms);

/**
 * Registers a game profiler category under the frame-total root.
 *
 * Names are copied and truncated to MISO_PROFILER_CATEGORY_NAME_MAX - 1 bytes.
 * Returns false when the profiler is missing, arguments are invalid, or the
 * category table is full.
 *
 * \param engine Engine whose profiler is active.
 * \param name Category display name.
 * \param rgba8 Category color as 0xRRGGBBAA.
 * \param out_category Receives the new category id.
 * \return true if the category was registered.
 */
bool miso_profiler_register_game_category(const MisoEngine *engine,
                                          const char *name,
                                          uint32_t rgba8,
                                          MisoProfilerCategoryId *out_category);

/**
 * Registers a game profiler category with an explicit parent category.
 *
 * \p parent may be MISO_PROFILER_CATEGORY_NONE or any active category id.
 * Returns false for invalid parents, invalid arguments, missing profiler state,
 * or a full category table.
 *
 * \param engine Engine whose profiler is active.
 * \param parent Parent category id or MISO_PROFILER_CATEGORY_NONE.
 * \param name Category display name.
 * \param rgba8 Category color as 0xRRGGBBAA.
 * \param out_category Receives the new category id.
 * \return true if the category was registered.
 */
bool miso_profiler_register_game_category_child(const MisoEngine *engine,
                                                MisoProfilerCategoryId parent,
                                                const char *name,
                                                uint32_t rgba8,
                                                MisoProfilerCategoryId *out_category);

/**
 * Copies profiler categories, rolling frame history, FPS values, and target
 * frame time into caller storage.
 *
 * Returns false when profiler state or output storage is missing.
 *
 * \param engine Engine whose profiler is active.
 * \param out_snapshot Receives a copy of profiler state/history.
 * \return true if a snapshot was written.
 */
bool miso_profiler_get_snapshot(const MisoEngine *engine, MisoProfilerSnapshot *out_snapshot);

/**
 * Returns the display name for a profiler category.
 *
 * Invalid categories return the static string "unknown".
 *
 * \param engine Engine whose profiler is active.
 * \param category Category id to query.
 * \return Category name string.
 */
const char *miso_profiler_get_category_name(const MisoEngine *engine, MisoProfilerCategoryId category);

/**
 * Returns the configured 0xRRGGBBAA color for a profiler category.
 *
 * Invalid categories return white.
 *
 * \param engine Engine whose profiler is active.
 * \param category Category id to query.
 * \return Category color as 0xRRGGBBAA.
 */
uint32_t miso_profiler_get_category_color(const MisoEngine *engine, MisoProfilerCategoryId category);

/**
 * Copies rolling FPS statistics.
 *
 * Each output pointer is optional. Provided output pointers must not alias each
 * other. Missing profiler state writes 0 to provided outputs.
 *
 * \param engine Engine whose profiler is active.
 * \param out_min Optional destination for minimum FPS.
 * \param out_avg Optional destination for average FPS.
 * \param out_max Optional destination for maximum FPS.
 */
void miso_profiler_get_fps(const MisoEngine *engine,
                           float *restrict out_min,
                           float *restrict out_avg,
                           float *restrict out_max);

/**
 * Returns the latest completed frame duration in milliseconds.
 *
 * Returns 0 if no frame history exists.
 *
 * \param engine Engine whose profiler is active.
 * \return Latest frame duration in milliseconds.
 */
float miso_profiler_get_last_frame_time_ms(const MisoEngine *engine);

/**
 * Creates persistent text objects used by the built-in profiler overlay.
 *
 * The supplied font must be a valid MisoFontHandle. Categories registered after
 * this call do not get overlay labels until the overlay is reinitialized.
 *
 * \param engine Engine whose profiler/text renderer is active.
 * \param font Font handle used by overlay labels.
 */
void miso_profiler_overlay_init(const MisoEngine *engine, MisoFontHandle font);

/**
 * Destroys text objects owned by the built-in profiler overlay.
 *
 * \param engine Engine whose profiler overlay should shut down.
 */
void miso_profiler_overlay_shutdown(const MisoEngine *engine);

/**
 * Renders the built-in profiler overlay at a UI pixel position.
 *
 * This should be called from a UI/debug render hook after a frame history exists
 * and after the overlay has been initialized. Invalid or missing text handles
 * are skipped.
 *
 * \param engine Engine whose profiler/text renderer is active.
 * \param position Top-left UI position for the overlay.
 */
void miso_profiler_overlay_render(const MisoEngine *engine, SDL_FPoint position);

#endif
