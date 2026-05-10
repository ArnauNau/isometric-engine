#ifndef MISO_DEBUG_UI_H
#define MISO_DEBUG_UI_H

#include "miso_engine.h"
#include "miso_events.h"

struct nk_context;

/**
 * Initializes the Nuklear debug UI bridge for the active renderer.
 *
 * The renderer must already be initialized by miso_create(). Repeated calls are
 * allowed and return MISO_OK after the first successful initialization. If the
 * font cannot be loaded, Nuklear falls back to its default font.
 *
 * \param engine Engine whose renderer is active.
 * \param font_path Path to a font file for Nuklear, or NULL for fallback.
 * \param font_size Font size in points before pixel-density scaling.
 * \return MISO_OK on success or an initialization error.
 */
MisoResult miso_debug_ui_init(const MisoEngine *engine, const char *font_path, float font_size);

/**
 * Shuts down the debug UI bridge.
 *
 * Passing through when it is not initialized is allowed.
 */
void miso_debug_ui_shutdown(void);

/**
 * Begins a Nuklear input collection block.
 *
 * Call before feeding all events for the frame. It is a no-op when debug UI is
 * not initialized.
 */
void miso_debug_ui_begin_input(void);

/**
 * Ends a Nuklear input collection block.
 *
 * Call after feeding all events for the frame. It is a no-op when debug UI is
 * not initialized.
 */
void miso_debug_ui_end_input(void);

/**
 * Feeds a converted MisoEvent into Nuklear.
 *
 * Mouse coordinates are scaled by the current window pixel density. Mouse wheel
 * deltas are only forwarded to Nuklear when UI is active or hovered, so gameplay
 * may use a false return value for world zoom. The return value indicates
 * whether Nuklear currently has active or hovered UI and is useful for deciding
 * whether gameplay should consume the same input.
 *
 * \param event Event to feed.
 * \return true if Nuklear has active or hovered UI after processing.
 */
bool miso_debug_ui_feed_event(const MisoEvent *event);

/**
 * Prepares the renderer for Nuklear rendering.
 *
 * This ends the current renderer pass so Nuklear can issue its own commands.
 *
 * \param engine Engine whose renderer is active.
 */
void miso_debug_ui_prepare_render(const MisoEngine *engine);

/**
 * Returns the raw Nuklear context, or NULL when not initialized.
 *
 * The pointer is owned by the debug UI bridge and is invalidated by
 * miso_debug_ui_shutdown().
 *
 * \return Nuklear context pointer, or NULL.
 */
struct nk_context *miso_debug_ui_get_context(void);

/**
 * Returns the UI scale captured at initialization.
 *
 * Runtime event feeding separately queries current window density for input
 * scaling; this value is primarily useful for sizing debug UI layout.
 *
 * \return Debug UI scale captured during initialization.
 */
float miso_debug_ui_get_scale(void);

/**
 * Renders Nuklear commands into the current frame.
 *
 * If required renderer command/swapchain state is missing, pending Nuklear
 * commands are cleared and nothing is drawn.
 *
 * \param engine Engine whose renderer is active.
 */
void miso_debug_ui_render(const MisoEngine *engine);

#endif
