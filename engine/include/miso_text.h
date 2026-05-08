#ifndef MISO_TEXT_H
#define MISO_TEXT_H

#include "miso_engine.h"
#include "miso_render.h"

typedef Uint32 MisoTextHandle;

typedef struct MisoTextMetrics {
    float width;
    float height;
} MisoTextMetrics;

/**
 * Creates a retained UI text object from a loaded font.
 *
 * Retained text is the normal path for labels, HUD values, buttons, overlays,
 * and other UI text that has identity across frames, whether it changes rarely
 * or every frame. The handle owns one SDL_ttf text object and keeps cached
 * metrics. \p initial_text may be NULL, which creates an empty string. Text
 * handle 0 is invalid.
 *
 * \param engine Engine whose renderer/text engine is active.
 * \param font Font handle to use.
 * \param initial_text Initial string, or NULL for empty.
 * \param out_text Receives the created text handle.
 * \return MISO_OK on success or an error code.
 */
MisoResult
miso_text_create(const MisoEngine *engine, MisoFontHandle font, const char *initial_text, MisoTextHandle *out_text);

/**
 * Destroys a retained text handle.
 *
 * Invalid handles are ignored. Text handles are also destroyed automatically
 * when their source font is destroyed or the renderer shuts down.
 *
 * \param engine Engine whose renderer/text engine is active.
 * \param text Text handle to destroy.
 */
void miso_text_destroy(const MisoEngine *engine, MisoTextHandle text);

/**
 * Returns whether a text handle currently refers to a live text object.
 *
 * \param engine Engine whose renderer/text engine is active.
 * \param text Text handle to test.
 * \return true if the handle is valid.
 */
bool miso_text_is_valid(const MisoEngine *engine, MisoTextHandle text);

/**
 * Replaces a retained text object's string.
 *
 * Passing NULL is treated as an empty string. Passing the same string is a
 * cheap no-op; layout and cached metrics are refreshed only when the string
 * changes. Invalid handles return MISO_ERR_NOT_FOUND.
 *
 * \param engine Engine whose renderer/text engine is active.
 * \param text Text handle to update.
 * \param string Replacement string, or NULL for empty.
 * \return MISO_OK on success or an error code.
 */
MisoResult miso_text_set_string(const MisoEngine *engine, MisoTextHandle text, const char *string);

/**
 * Copies cached pixel metrics for a retained text object.
 *
 * Returns false for invalid handles or NULL output storage.
 *
 * \param engine Engine whose renderer/text engine is active.
 * \param text Text handle to query.
 * \param out_metrics Receives cached width and height.
 * \return true if metrics were written.
 */
bool miso_text_get_metrics(const MisoEngine *engine, MisoTextHandle text, MisoTextMetrics *out_metrics);

/**
 * Enables or disables the optional background rectangle drawn with this text.
 *
 * Invalid handles are ignored.
 *
 * \param engine Engine whose renderer/text engine is active.
 * \param text Text handle to update.
 * \param enabled Whether to draw the background rectangle.
 */
void miso_text_set_background_enabled(const MisoEngine *engine, MisoTextHandle text, bool enabled);

/**
 * Configures the optional background rectangle for this text.
 *
 * Colors use 0xRRGGBBAA. Negative padding is clamped to 0. Invalid handles are
 * ignored.
 *
 * \param engine Engine whose renderer/text engine is active.
 * \param text Text handle to update.
 * \param rgba8 Background color as 0xRRGGBBAA.
 * \param padding Background padding in pixels.
 */
void miso_text_set_background_style(const MisoEngine *engine, MisoTextHandle text, Uint32 rgba8, float padding);

/**
 * Submits a retained text object in UI pixel coordinates.
 *
 * If a background is enabled, it is drawn before the text using the cached text
 * metrics and configured padding. Invalid handles are ignored.
 *
 * \note The current implementation ignores \p rgba8; text color is controlled
 * by the underlying SDL_ttf text object/default renderer state.
 *
 * \param engine Engine whose renderer/text engine is active.
 * \param text Text handle to submit.
 * \param x UI x coordinate in pixels.
 * \param y UI y coordinate in pixels.
 * \param rgba8 Requested text color as 0xRRGGBBAA; currently ignored.
 */
void miso_render_submit_ui_text_handle(const MisoEngine *engine, MisoTextHandle text, float x, float y, Uint32 rgba8);

#endif
