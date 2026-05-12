#ifndef RENDERER_H
#define RENDERER_H

/* Private low-level renderer backend. Engine clients should use miso_render.h
 * or miso_render_diagnostics.h; engine internals should use
 * miso__renderer_backend.h. */

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
typedef struct RendererConfig {
    const char *sprite_shader_path;
    const char *geometry_shader_path;
    const char *ui_shader_path;
    const char *nuklear_shader_path;
} RendererConfig;

/**
 * @brief Result of decoding and uploading an image file through the low-level renderer.
 *
 * This is private renderer-boundary transport, not an engine resource handle.
 * The caller owns the returned GPU texture and can copy width/height into its
 * own handle table or asset metadata.
 */
typedef struct RendererTextureLoadResult {
    ///< Uploaded GPU texture, or NULL when loading failed.
    SDL_GPUTexture *texture;
    ///< Decoded image width in pixels.
    Uint32 width;
    ///< Decoded image height in pixels.
    Uint32 height;
} RendererTextureLoadResult;

/**
 * @brief Sprite instance data for GPU-batched rendering.
 *
 * Each instance represents one sprite in a batched draw call. The layout
 * must match the shader's InstanceData struct exactly (48 bytes, 16-byte aligned).
 *
 * @note For water tiles, set flags to 1.0 and provide tile_x/tile_y for wave
 *       phase calculation. The shader applies wave animation automatically.
 */
typedef struct {
    float x, y, z;        ///< World position (z used for depth sorting)
    float flags;          ///< Tile flags: 1.0 = water (shader-animated), 0.0 = normal
    float w, h;           ///< Sprite dimensions in world units
    float tile_x, tile_y; ///< Tile grid position (used for wave phase offset)
    float u, v, uw, vh;   ///< UV coordinates in texture atlas (u, v, width, height)
} SpriteInstance;

typedef enum RendererStatsQueueKind {
    RENDERER_STATS_QUEUE_SPRITE = 0,
    RENDERER_STATS_QUEUE_WORLD_GEOMETRY,
    RENDERER_STATS_QUEUE_LINE,
    RENDERER_STATS_QUEUE_UI_GEOMETRY,
    RENDERER_STATS_QUEUE_UI_TEXT,
    RENDERER_STATS_QUEUE_COUNT
} RendererStatsQueueKind;

typedef enum RendererStatsStreamKind {
    RENDERER_STATS_STREAM_SPRITE = 0,
    RENDERER_STATS_STREAM_WORLD_GEOMETRY,
    RENDERER_STATS_STREAM_LINE,
    RENDERER_STATS_STREAM_UI_GEOMETRY,
    RENDERER_STATS_STREAM_UI_TEXT_VERT,
    RENDERER_STATS_STREAM_UI_TEXT_INDEX,
    RENDERER_STATS_STREAM_COUNT
} RendererStatsStreamKind;

typedef enum RendererVSyncAcquireMode {
    // VSYNC waits for drawable availability before rendering this frame.
    RENDERER_VSYNC_ACQUIRE_BLOCKING = 0,
    // VSYNC polls drawable availability and skips present work when unavailable.
    RENDERER_VSYNC_ACQUIRE_PASSTHROUGH = 1
} RendererVSyncAcquireMode;

typedef struct RendererQueueStats {
    Uint32 cmd_count;
    Uint32 draw_calls;
} RendererQueueStats;

typedef struct RendererPassStats {
    Uint32 begin_calls;
    Uint32 end_calls;
    Uint32 world_passes;
    Uint32 ui_passes;
} RendererPassStats;

typedef struct RendererTimingStats {
    float frame_cpu_ms;
    float acquire_swapchain_ms;
    float record_commands_ms;
    float submit_ms;
} RendererTimingStats;

typedef struct RendererStreamStats {
    Uint32 used_bytes;
    Uint32 peak_bytes;
    Uint32 capacity_bytes;
    Uint32 uploaded_bytes;
    Uint32 overflow_count;
} RendererStreamStats;

typedef struct RendererFrameStats {
    RendererQueueStats queues[RENDERER_STATS_QUEUE_COUNT];
    RendererPassStats passes;
    RendererTimingStats timing;
    Uint32 render_pass_count;
    Uint32 draw_calls_world;
    Uint32 draw_calls_ui;
    Uint32 draw_calls_lines;
    Uint32 uploaded_bytes_sprite;
    Uint32 uploaded_bytes_world_geo;
    Uint32 uploaded_bytes_ui_geo;
    Uint32 uploaded_bytes_ui_text;
    Uint32 uploaded_bytes_line;
    Uint32 uploaded_bytes_total;
    Uint32 instances_submitted;
    Uint32 line_vertices_submitted;
    Uint32 texture_upload_count;
    Uint32 texture_upload_bytes;
    Uint32 transient_buffer_creations;
    RendererStreamStats streams[RENDERER_STATS_STREAM_COUNT];
} RendererFrameStats;

bool Renderer_Init(SDL_Window *window, const RendererConfig *config);
const char *Renderer_GetNuklearShaderPath(void);
void Renderer_Shutdown(void);
void Renderer_Resize(int width, int height);
void Renderer_SetVSync(bool enabled);

// Uploads an existing CPU surface to the GPU. The caller retains ownership of the surface.
SDL_GPUTexture *Renderer_CreateTextureFromSurface(SDL_Surface *surface);
SDL_GPUTexture *Renderer_CreateRGBA8Texture(Uint32 width, Uint32 height, const void *rgba8_pixels);
bool Renderer_UpdateRGBA8Texture(SDL_GPUTexture *texture, Uint32 width, Uint32 height, const void *rgba8_pixels);

/**
 * @brief Load an image with SDL's built-in surface loader and upload it.
 *
 * Current support follows SDL_LoadSurface(), primarily PNG/BMP in this build.
 * Unsupported or failed loads return a zero-initialized result.
 *
 * @param path Image file path.
 * @return Uploaded texture and decoded dimensions, or zero on failure.
 *
 * TODO(asset-loading): introduce an engine-owned image loader that can
 * deliberately choose SDL_LoadSurface or SDL_image per format/build mode before
 * broadening supported runtime formats.
 */
RendererTextureLoadResult Renderer_LoadTexture(const char *path);
void Renderer_DestroyTexture(SDL_GPUTexture *texture);

void Renderer_BeginFrame(void);
void Renderer_EndFrame(void);

/**
 * @brief Set the view-projection matrix for world-space rendering.
 *
 * This matrix transforms world coordinates to clip space. It should combine
 * the camera's view matrix and the projection matrix.
 *
 * @param viewProjMatrix Column-major 4x4 matrix (16 floats).
 */
void Renderer_SetViewProjection(const float *viewProjMatrix);

/**
 * @brief Set water animation parameters for shader-based waves.
 *
 * These parameters control how water tiles (SpriteInstance.flags == 1.0)
 * are animated in the vertex shader. Call this once per frame before
 * rendering water tiles.
 *
 * The wave formula uses: sin(time * 2π * speed + (tile_x + tile_y) * phase) * amplitude
 *
 * @param time      Current game time in seconds (typically from game clock).
 * @param speed     Wave cycle speed multiplier (1.0 = one cycle per second).
 * @param amplitude Wave height as fraction of tile height (0.0-1.0 typical).
 * @param phase     Phase offset multiplier for tile position (controls wave width).
 *
 * @see SpriteInstance for per-tile water flag.
 * @see miso_tile_scene_render_terrain() which uses these parameters automatically.
 */
void Renderer_SetWaterParams(float time, float speed, float amplitude, float phase);
void Renderer_SetSpriteTintOverlay(SDL_GPUTexture *texture, Uint32 width, Uint32 height, float strength);

/**
 * @brief Draw a batch of sprites using GPU instancing.
 *
 * Renders multiple sprites in a single draw call for optimal performance.
 * All sprites must use the same texture.
 *
 * @param texture   The texture atlas containing all sprite images.
 * @param instances Array of sprite instance data; must not alias renderer-owned
 *                  upload storage.
 * @param count     Number of sprites to draw.
 *
 * @pre Renderer_BeginFrame() has been called.
 * @pre Renderer_SetViewProjection() has been called.
 */
void Renderer_DrawSprites(SDL_GPUTexture *texture, const SpriteInstance *restrict instances, int count);

// Update the camera/view projection
void Renderer_DrawLine(float x1, float y1, float z1, float x2, float y2, float z2, SDL_FColor color);
// Draw a batched line list. `vertex_count` must be even (2 vertices per segment).
// The vertex array must not alias renderer-owned upload storage.
void Renderer_DrawLineBatch(const float *restrict vertices_xyz, int vertex_count, SDL_FColor color);
// The vertex array must not alias renderer-owned upload storage.
void Renderer_DrawGeometry(const SDL_Vertex *restrict vertices, int count);

TTF_TextEngine *Renderer_GetTextEngine(void);
[[deprecated("Use Renderer_UI_DrawText instead.")]]
void Renderer_DrawText(TTF_Text *text, float x, float y);
// Screen-space geometry rendering (for UI elements like profiler)
// Coordinates are in screen pixels: (0,0) = top-left
[[deprecated("Use Renderer_UI_FillRect and other UI functions instead.")]]
void Renderer_DrawGeometryScreenSpace(const SDL_Vertex *vertices, int count);

// ============================================================================
// Low-Level Batch Flush API (used by ui_batch.c)
// ============================================================================
// These functions render pre-batched data efficiently.
// Prefer using the high-level UI_* functions from ui_batch.h instead.

// Atlas info for batched text rendering
typedef struct {
    SDL_GPUTexture *atlas;
    int start_index;
    int index_count;
} UITextAtlasInfo;

// Flush screen-space geometry (single draw call). The vertex array must not
// alias renderer-owned upload storage.
void Renderer_FlushUIGeometry(const SDL_Vertex *restrict vertices, int count);

// Flush screen-space text (one draw call per atlas). Input arrays must not
// alias each other or renderer-owned upload storage.
void Renderer_FlushUIText(const float *restrict vertices,
                          int vertex_count,
                          const int *restrict indices,
                          int index_count,
                          const UITextAtlasInfo *restrict atlases,
                          int atlas_count);

/* ------------------ DEBUG UTILITIES ------------------ */
// Debug: Draw a texture as a screen-space quad
void Renderer_DrawTextureDebug(SDL_GPUTexture *texture, float x, float y, float width, float height);

// Debug: Draw a filled colored quad using geometry pipeline (to verify rendering works)
void Renderer_DrawFilledQuadDebug(float x, float y, float width, float height, SDL_FColor color);

void Renderer_SetPresentMode(SDL_GPUPresentMode mode);
SDL_GPUPresentMode Renderer_GetPresentMode(void);
void Renderer_SetVSyncAcquireMode(RendererVSyncAcquireMode mode);
RendererVSyncAcquireMode Renderer_GetVSyncAcquireMode(void);
/*
 * Controls swapchain queue depth (1..3) independently from renderer upload stream buffering.
 * Default is 1 to favor freshest-frame throughput by dropping stale frames under backpressure.
 */
bool Renderer_SetAllowedFramesInFlight(Uint32 allowed_frames_in_flight);
Uint32 Renderer_GetAllowedFramesInFlight(void);
void Renderer_SetUploadSuppressed(bool enabled);
bool Renderer_GetUploadSuppressed(void);
const RendererFrameStats *Renderer_GetFrameStats(void);

#endif // RENDERER_H
