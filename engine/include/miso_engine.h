#ifndef MISO_ENGINE_H
#define MISO_ENGINE_H

#include <SDL3/SDL.h>

#define MISO_VERSION "0.3.1"
#define MISO_PATH_MAX 1024U

#include "miso_events.h"

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

/**
 * Client fixed-step simulation callback.
 *
 * \param user Opaque pointer supplied to miso_run_simulation_ticks().
 * \param fixed_dt_seconds Fixed simulation timestep in seconds.
 */
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
    /**
     * Receives converted input/window events after miso_poll_event().
     *
     * \param game_ctx Opaque game context registered with miso_game_register().
     * \param event Converted event owned by the engine for the duration of the call.
     */
    void (*on_event)(void *game_ctx, const MisoEvent *event);

    /**
     * Runs one deterministic game simulation tick.
     *
     * \param game_ctx Opaque game context registered with miso_game_register().
     * \param fixed_dt_seconds Fixed simulation timestep in seconds.
     */
    void (*on_sim_tick)(void *game_ctx, float fixed_dt_seconds);

    /**
     * Renders the world layer for a normal frame or live-resize maintenance redraw.
     *
     * \param game_ctx Opaque game context registered with miso_game_register().
     * \param engine Engine passed to render submission APIs.
     */
    void (*on_render_world)(void *game_ctx, const MisoEngine *engine);

    /**
     * Renders the UI layer for a normal frame.
     *
     * This hook is not guaranteed during live-resize maintenance redraws.
     *
     * \param game_ctx Opaque game context registered with miso_game_register().
     * \param engine Engine passed to render submission APIs.
     */
    void (*on_render_ui)(void *game_ctx, const MisoEngine *engine);

    /**
     * Renders the debug layer for a normal frame.
     *
     * This hook is not guaranteed during live-resize maintenance redraws.
     *
     * \param game_ctx Opaque game context registered with miso_game_register().
     * \param engine Engine passed to render/debug APIs.
     */
    void (*on_render_debug)(void *game_ctx, const MisoEngine *engine);

    /**
     * Serializes game-owned payload bytes for miso_save_game().
     *
     * The engine frees out_payload->data with SDL_free() after writing.
     *
     * \param game_ctx Opaque game context registered with miso_game_register().
     * \param out_payload Receives payload pointer and size.
     * \param out_payload_version Receives game-defined payload version.
     * \return MISO_OK on success or an error code.
     */
    MisoResult (*on_save)(const void *game_ctx, MisoByteBuffer *out_payload, uint32_t *out_payload_version);

    /**
     * Loads game-owned payload bytes from miso_load_game().
     *
     * The payload pointer is temporary and valid only for the duration of the call.
     *
     * \param game_ctx Opaque game context registered with miso_game_register().
     * \param payload Payload bytes from the save envelope, or NULL for empty payloads.
     * \param payload_size Number of payload bytes.
     * \param payload_version Game-defined payload version from the save envelope.
     * \return MISO_OK on success or an error code.
     */
    MisoResult (*on_load)(void *game_ctx, const uint8_t *payload, size_t payload_size, uint32_t payload_version);

    /**
     * Resets game state after registration.
     *
     * \param game_ctx Opaque game context registered with miso_game_register().
     */
    void (*on_reset)(void *game_ctx);

    /**
     * Computes a deterministic game state hash for diagnostics/replay tooling.
     *
     * \param game_ctx Opaque game context registered with miso_game_register().
     * \return Game-defined state hash.
     */
    uint64_t (*on_state_hash)(const void *game_ctx);
} MisoGameHooks;

/**
 * Creates an engine instance, SDL window, renderer, UI subsystem, profiler, and
 * the initial camera storage.
 *
 * If \p cfg is NULL, default settings are used. Non-positive simulation tick
 * settings are replaced with defaults, and a NULL window title becomes "miso".
 * The data root is either taken from MisoConfig::data_root or auto-resolved
 * from the executable base path.
 *
 * \param cfg Optional configuration.
 * \param out_engine Receives the created engine on success and NULL on failure.
 * \return MISO_OK on success; otherwise an initialization, GPU, argument, or
 * out-of-memory error.
 */
MisoResult miso_create(const MisoConfig *cfg, MisoEngine **out_engine);

/**
 * Destroys an engine created by miso_create().
 *
 * This also shuts down engine-owned render resources, profiler state, the SDL
 * window, and SDL itself. Passing NULL is allowed.
 *
 * \param engine Engine to destroy, or NULL.
 */
void miso_destroy(MisoEngine *engine);

/**
 * Starts a frame and updates real-time delta accounting.
 *
 * Call this once at the top of the external frame loop before polling events,
 * running simulation ticks, and ending the frame. Pending resize state is
 * applied before the returned delta and accumulator are updated.
 *
 * \param engine Engine instance.
 * \return true if the engine is running and a frame was started; false after
 * quit or for an invalid engine.
 */
bool miso_begin_frame(MisoEngine *engine);

/**
 * Ends the current frame and invokes registered render hooks in world, UI, then
 * debug order.
 *
 * During live window resizing, the engine may already have performed a
 * world-only maintenance redraw for the current frame. In that case this call
 * only finalizes frame/profiler bookkeeping and does not render UI/debug for
 * that maintenance redraw.
 *
 * \param engine Engine instance.
 */
void miso_end_frame(MisoEngine *engine);

/**
 * Queries the current SDL drawable size in pixels.
 *
 * Output pointers are left untouched if any required argument is invalid. The
 * output pointers must not alias each other.
 *
 * \param engine Engine instance.
 * \param out_width Receives drawable width in pixels.
 * \param out_height Receives drawable height in pixels.
 */
void miso_get_window_size_pixels(const MisoEngine *engine, int *restrict out_width, int *restrict out_height);

/**
 * Returns the SDL window pixel density.
 *
 * Invalid engines return 1.0f.
 *
 * \param engine Engine instance.
 * \return Current SDL window pixel density, or 1.0f for invalid input.
 */
float miso_get_window_pixel_density(const MisoEngine *engine);

/**
 * Resolves an asset path against the engine data root.
 *
 * Absolute paths are copied through. Relative paths are first resolved under
 * the data root and may fall back to a sibling-root lookup used by app bundle
 * layouts. The function can still return a candidate path even when the final
 * primary path does not exist, so callers that require existence should check
 * the file themselves.
 *
 * \param engine Engine instance.
 * \param path Relative or absolute input path.
 * \param out_path Destination buffer for the resolved path.
 * \param out_path_size Size of \p out_path in bytes.
 * \return true if a path string was written to \p out_path.
 */
bool miso_resolve_asset_path(const MisoEngine *engine, const char *path, char *out_path, size_t out_path_size);

/**
 * Returns the resolved data root string for this engine.
 *
 * The returned pointer is owned by the engine and remains valid until
 * miso_destroy().
 *
 * \param engine Engine instance.
 * \return Engine-owned data root string, or NULL for invalid input/no root.
 */
const char *miso_get_data_root(const MisoEngine *engine);

/**
 * Runs fixed-step simulation ticks accumulated since the previous frame.
 *
 * For each tick, \p tick_fn is called first when non-NULL, then the registered
 * game on_sim_tick hook is called when present. The number of ticks processed
 * in one frame is capped by MisoConfig::max_sim_steps_per_frame; any remaining
 * accumulator is retained for later frames.
 *
 * \param engine Engine instance.
 * \param tick_fn Optional client tick callback.
 * \param user Opaque pointer passed to \p tick_fn.
 */
void miso_run_simulation_ticks(MisoEngine *engine, MisoSimTickFn tick_fn, void *user);

/**
 * Returns the wall-clock seconds measured by the last miso_begin_frame().
 *
 * Invalid engines return 0.0f.
 *
 * \param engine Engine instance.
 * \return Last real frame delta in seconds.
 */
float miso_get_real_delta_seconds(const MisoEngine *engine);

/**
 * Returns the fractional simulation accumulator for interpolation.
 *
 * The value is accumulator / fixed_step and is normally in the 0..1 range, but
 * can be higher when the simulation is behind because the per-frame tick cap
 * was reached.
 *
 * \param engine Engine instance.
 * \return Fractional fixed-tick accumulator.
 */
float miso_get_interpolation_alpha(const MisoEngine *engine);

/**
 * Registers game callbacks and an opaque game context with the engine.
 *
 * Existing hooks are replaced. If the supplied hooks include on_reset, it is
 * called immediately after registration.
 *
 * \param engine Engine instance.
 * \param hooks Callback table copied into the engine.
 * \param game_ctx Opaque context passed back to game callbacks.
 * \return MISO_OK on success, or MISO_ERR_INVALID_ARG.
 */
MisoResult miso_game_register(MisoEngine *engine, const MisoGameHooks *hooks, void *game_ctx);

/**
 * Polls one recognized SDL event and converts it to MisoEvent.
 *
 * Recognized events are also forwarded to the registered game on_event hook
 * before this function returns. Duplicate resize notifications for the same
 * pixel size are suppressed for game dispatch. Unknown SDL events are skipped
 * internally rather than terminating the poll loop.
 *
 * When no event is available, \p out_event is set to MISO_EVENT_NONE and false
 * is returned. A quit event requests engine shutdown, causing later
 * miso_begin_frame() calls to return false.
 *
 * \param engine Engine instance.
 * \param out_event Receives the converted event or MISO_EVENT_NONE.
 * \return true when an event was written, false when no recognized event is available.
 */
bool miso_poll_event(MisoEngine *engine, MisoEvent *out_event);

#endif
