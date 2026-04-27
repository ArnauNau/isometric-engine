#ifndef MISO_RENDER_H
#define MISO_RENDER_H

#include "miso_camera.h"
#include "miso_engine.h"

typedef uint32_t MisoTextureHandle;
typedef uint32_t MisoFontHandle;

typedef struct MisoSpriteInstance {
    float x;
    float y;
    float z;
    float flags;
    float w;
    float h;
    float tile_x;
    float tile_y;
    float u;
    float v;
    float uw;
    float vh;
} MisoSpriteInstance;

typedef struct MisoWorldVertex {
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
} MisoWorldVertex;

/**
 * Loads an image file into the renderer texture table.
 *
 * The path is used as provided; call miso_resolve_asset_path() first for
 * data-root relative assets. Texture handle 0 is never returned and represents
 * an invalid texture.
 *
 * \param engine Engine whose renderer is active.
 * \param path Image file path.
 * \param out_texture Receives the created texture handle.
 * \return MISO_OK on success or an error code.
 */
MisoResult miso_render_load_texture(const MisoEngine *engine, const char *path, MisoTextureHandle *out_texture);

/**
 * Destroys a texture handle.
 *
 * Invalid or already-destroyed handles are ignored.
 *
 * \param engine Engine whose renderer is active.
 * \param texture Texture handle to destroy.
 */
void miso_render_destroy_texture(const MisoEngine *engine, MisoTextureHandle texture);

/**
 * Loads a TrueType/OpenType font for UI text rendering.
 *
 * The path is used as provided and \p point_size must be positive. Font handle
 * 0 is invalid. Destroying a font also invalidates text handles created from it.
 *
 * \param engine Engine whose renderer/text engine is active.
 * \param path Font file path.
 * \param point_size Font size in points.
 * \param out_font Receives the created font handle.
 * \return MISO_OK on success or an error code.
 */
MisoResult
miso_render_load_font(const MisoEngine *engine, const char *path, float point_size, MisoFontHandle *out_font);

/**
 * Destroys a font handle and any cached text objects that use it.
 *
 * Invalid or already-destroyed handles are ignored.
 *
 * \param engine Engine whose renderer/text engine is active.
 * \param font Font handle to destroy.
 */
void miso_render_destroy_font(const MisoEngine *engine, MisoFontHandle font);

/**
 * Enables or disables renderer vsync using the backend's default vsync mode.
 *
 * \param engine Engine whose renderer is active.
 * \param enabled Whether vsync should be enabled.
 */
void miso_render_set_vsync(const MisoEngine *engine, bool enabled);

/**
 * Returns whether the renderer currently reports vsync enabled.
 *
 * \param engine Engine whose renderer is active.
 * \return true when vsync is enabled.
 */
bool miso_render_get_vsync(const MisoEngine *engine);

/**
 * Selects the camera transform used for subsequent world submissions.
 *
 * This should be called from an on_render_world hook before submitting world
 * sprites, geometry, or lines. An invalid camera id falls back to an identity
 * view-projection matrix.
 *
 * \param engine Engine whose renderer is active.
 * \param camera_id Camera used for world transforms.
 */
void miso_render_begin_world(const MisoEngine *engine, MisoCameraId camera_id);

/**
 * Updates shader water animation parameters for later sprite draws.
 *
 * The exact interpretation is shader-defined; current Metal shaders use these
 * values to animate water-like sprite UV deformation.
 *
 * \param engine Engine whose renderer is active.
 * \param time Animation time value.
 * \param speed Animation speed multiplier.
 * \param amplitude Animation amplitude.
 * \param phase Animation phase offset.
 */
void miso_render_set_water_params(const MisoEngine *engine, float time, float speed, float amplitude, float phase);

/**
 * Submits batched world sprites using a loaded texture.
 *
 * Invalid texture handles, NULL instance arrays, or non-positive counts are
 * ignored. MisoSpriteInstance fields are consumed by the active sprite shader
 * and batching path.
 *
 * \param engine Engine whose renderer is active.
 * \param texture Texture handle used by all submitted sprites.
 * \param instances Sprite instance array.
 * \param count Number of sprite instances.
 */
void miso_render_submit_sprites(const MisoEngine *engine,
                                MisoTextureHandle texture,
                                const MisoSpriteInstance *instances,
                                int count);

/**
 * Submits colored world-space triangles.
 *
 * Vertices are interpreted as a triangle list in the active world transform.
 * NULL input, non-positive counts, or temporary allocation failure are ignored.
 *
 * \param engine Engine whose renderer is active.
 * \param vertices Triangle-list vertices.
 * \param count Number of vertices.
 */
void miso_render_submit_world_geometry(const MisoEngine *engine, const MisoWorldVertex *vertices, int count);

/**
 * Submits world-space line segments.
 *
 * \p vertices_xyz contains x/y/z triples. \p vertex_count is a count of
 * vertices, not floats, and must be even because each pair forms one segment.
 * Invalid input is ignored. Colors use 0xRRGGBBAA.
 *
 * \param engine Engine whose renderer is active.
 * \param vertices_xyz Packed x/y/z vertex triples.
 * \param vertex_count Number of vertices in \p vertices_xyz.
 * \param rgba8 Line color as 0xRRGGBBAA.
 */
void miso_render_submit_world_lines(const MisoEngine *engine,
                                    const float *vertices_xyz,
                                    int vertex_count,
                                    uint32_t rgba8);

/**
 * Ends world submission for readability.
 *
 * The current implementation is a no-op; flushing happens in the renderer frame
 * orchestration.
 *
 * \param engine Engine whose renderer is active.
 */
void miso_render_end_world(const MisoEngine *engine);

/**
 * Begins UI submission for readability.
 *
 * The current implementation is a no-op. UI coordinates are pixel-space.
 *
 * \param engine Engine whose renderer is active.
 */
void miso_render_begin_ui(const MisoEngine *engine);

/**
 * Submits a filled UI rectangle in pixel coordinates.
 *
 * Colors use 0xRRGGBBAA.
 *
 * \param engine Engine whose renderer is active.
 * \param x UI x coordinate in pixels.
 * \param y UI y coordinate in pixels.
 * \param w Rectangle width in pixels.
 * \param h Rectangle height in pixels.
 * \param rgba8 Fill color as 0xRRGGBBAA.
 */
void miso_render_submit_ui_rect(const MisoEngine *engine, float x, float y, float w, float h, uint32_t rgba8);

/**
 * Submits immediate UI text using a font handle.
 *
 * Empty strings, NULL strings, and invalid fonts are ignored. The current
 * immediate text path reuses one cached text object per font, so persistent or
 * frequently updated labels should prefer MisoTextHandle APIs.
 *
 * \note The current implementation ignores \p rgba8; text color is controlled
 * by the underlying SDL_ttf text object/default renderer state.
 *
 * \param engine Engine whose renderer/text engine is active.
 * \param font Font handle to use.
 * \param text Text string to draw.
 * \param x UI x coordinate in pixels.
 * \param y UI y coordinate in pixels.
 * \param rgba8 Requested text color as 0xRRGGBBAA; currently ignored.
 */
void miso_render_submit_ui_text(
    const MisoEngine *engine, MisoFontHandle font, const char *text, float x, float y, uint32_t rgba8);

/**
 * Flushes queued UI drawing.
 *
 * Call this after UI submissions in an on_render_ui hook.
 *
 * \param engine Engine whose renderer is active.
 */
void miso_render_end_ui(const MisoEngine *engine);

#endif
