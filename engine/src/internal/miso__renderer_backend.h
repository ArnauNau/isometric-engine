#ifndef MISO__RENDERER_BACKEND_H
#define MISO__RENDERER_BACKEND_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>

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

bool miso__renderer_init(SDL_Window *window);
void miso__renderer_shutdown(void);
void miso__renderer_resize(int width, int height);
void miso__renderer_set_vsync(bool enabled);
void miso__renderer_begin_frame(void);
void miso__renderer_end_frame(void);

SDL_GPUTexture *miso__renderer_load_texture(const char *path);
void miso__renderer_destroy_texture(SDL_GPUTexture *texture);
void miso__renderer_set_view_projection(const float *view_projection);
void miso__renderer_set_water_params(float time, float speed, float amplitude, float phase);
void miso__renderer_draw_sprites(SDL_GPUTexture *texture, const void *instances, int count);
void miso__renderer_draw_geometry(const SDL_Vertex *vertices, int count);
bool miso__renderer_copy_frame_stats(MisoRendererFrameStatsSnapshot *out_stats);
TTF_TextEngine *miso__renderer_get_text_engine(void);

void miso__renderer_ui_init(void);
void miso__renderer_ui_shutdown(void);
void miso__renderer_ui_fill_rect(float x, float y, float w, float h, SDL_FColor color);
void miso__renderer_ui_text(TTF_Text *text, float x, float y);
void miso__renderer_ui_flush(void);

SDL_Window *miso__renderer_get_window(void);
void miso__renderer_end_render_pass(void);

#endif
