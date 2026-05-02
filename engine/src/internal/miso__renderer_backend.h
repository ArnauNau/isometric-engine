#ifndef MISO__RENDERER_BACKEND_H
#define MISO__RENDERER_BACKEND_H

#include "miso_engine.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

typedef struct MisoRendererFrameStatsSnapshot {
    struct {
        Uint32 cmd_count;
        Uint32 draw_calls;
    } queues[5];
    struct {
        Uint32 begin_calls;
        Uint32 end_calls;
        Uint32 world_passes;
        Uint32 ui_passes;
    } passes;
    struct {
        float frame_cpu_ms;
        float acquire_swapchain_ms;
        float record_commands_ms;
        float submit_ms;
    } timing;
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
    struct {
        Uint32 used_bytes;
        Uint32 peak_bytes;
        Uint32 capacity_bytes;
        Uint32 uploaded_bytes;
        Uint32 overflow_count;
    } streams[6];
} MisoRendererFrameStatsSnapshot;

bool miso__renderer_init(const MisoEngine *engine, SDL_Window *window);
void miso__renderer_shutdown(void);
void miso__renderer_resize(int width, int height);
void miso__renderer_set_vsync(bool enabled);
bool miso__renderer_get_vsync(void);
void miso__renderer_begin_frame(void);
void miso__renderer_end_frame(void);

/**
 * Loads an image through the renderer and returns the native GPU texture.
 *
 * This is an engine-internal bridge. Public code should use MisoTextureHandle
 * and miso_render_get_texture_info().
 *
 * \param path Image file path.
 * \param out_width Optional output for decoded image width in pixels.
 * \param out_height Optional output for decoded image height in pixels.
 * \return Native GPU texture, or NULL on failure.
 */
SDL_GPUTexture *miso__renderer_load_texture(const char *path, uint32_t *out_width, uint32_t *out_height);
void miso__renderer_destroy_texture(SDL_GPUTexture *texture);
SDL_GPUTexture *miso__renderer_create_rgba8_texture(int width, int height, const void *rgba8_pixels);
bool miso__renderer_update_rgba8_texture(SDL_GPUTexture *texture, int width, int height, const void *rgba8_pixels);
void miso__renderer_set_view_projection(const float *view_projection);
void miso__renderer_set_water_params(float time, float speed, float amplitude, float phase);
void miso__renderer_set_sprite_tint_overlay(SDL_GPUTexture *texture, int width, int height, float strength);
void miso__renderer_draw_sprites(SDL_GPUTexture *texture, const void *instances, int count);
void miso__renderer_draw_native_sprites(void *texture, const void *instances, int count);
void miso__renderer_draw_line_batch(const float *vertices_xyz, int vertex_count, SDL_FColor color);
void miso__renderer_draw_geometry(const SDL_Vertex *vertices, int count);
void miso__renderer_draw_texture_debug(void *texture, float x, float y, float width, float height);
bool miso__renderer_copy_frame_stats(MisoRendererFrameStatsSnapshot *out_stats);
TTF_TextEngine *miso__renderer_get_text_engine(void);
void miso__renderer_set_present_mode(int mode);
int miso__renderer_get_present_mode(void);
void miso__renderer_set_vsync_acquire_mode(int mode);
int miso__renderer_get_vsync_acquire_mode(void);
bool miso__renderer_set_allowed_frames_in_flight(Uint32 allowed_frames_in_flight);
Uint32 miso__renderer_get_allowed_frames_in_flight(void);
void miso__renderer_set_upload_suppressed(bool enabled);
bool miso__renderer_get_upload_suppressed(void);

void miso__renderer_ui_init(void);
void miso__renderer_ui_shutdown(void);
void miso__renderer_ui_fill_rect(float x, float y, float w, float h, SDL_FColor color);
void miso__renderer_ui_text(TTF_Text *text, float x, float y);
void miso__renderer_ui_flush(void);

SDL_Window *miso__renderer_get_window(void);
void miso__renderer_end_render_pass(void);

#endif
