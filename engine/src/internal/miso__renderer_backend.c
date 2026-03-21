#include "miso__renderer_backend.h"

#include "miso__paths.h"
#include "renderer/renderer.h"
#include "renderer/renderer_internal.h"
#include "renderer/ui.h"

_Static_assert(sizeof(MisoRendererFrameStatsSnapshot) == sizeof(RendererFrameStats),
               "MisoRendererFrameStatsSnapshot must match RendererFrameStats layout");

bool miso__renderer_init(const MisoEngine *const engine, SDL_Window *const window) {
    char sprite_shader_path[MISO_PATH_MAX];
    char geometry_shader_path[MISO_PATH_MAX];
    char ui_shader_path[MISO_PATH_MAX];
    char nuklear_shader_path[MISO_PATH_MAX];

    if (!miso__resolve_asset_path(engine, "shaders/sprite.metal", sprite_shader_path, sizeof(sprite_shader_path)) ||
        !miso__resolve_asset_path(
            engine, "shaders/geometry.metal", geometry_shader_path, sizeof(geometry_shader_path)) ||
        !miso__resolve_asset_path(engine, "shaders/ui.metal", ui_shader_path, sizeof(ui_shader_path)) ||
        !miso__resolve_asset_path(engine, "shaders/nuklear.metal", nuklear_shader_path, sizeof(nuklear_shader_path))) {
        return false;
    }

    const RendererConfig config = {
        .sprite_shader_path = sprite_shader_path,
        .geometry_shader_path = geometry_shader_path,
        .ui_shader_path = ui_shader_path,
        .nuklear_shader_path = nuklear_shader_path,
    };
    return Renderer_Init(window, &config);
}

void miso__renderer_shutdown(void) {
    Renderer_Shutdown();
}

void miso__renderer_resize(int width, int height) {
    Renderer_Resize(width, height);
}

void miso__renderer_set_vsync(bool enabled) {
    Renderer_SetVSync(enabled);
}

bool miso__renderer_get_vsync(void) {
    const SDL_GPUPresentMode mode = Renderer_GetPresentMode();
    return mode != SDL_GPU_PRESENTMODE_IMMEDIATE;
}

void miso__renderer_begin_frame(void) {
    Renderer_BeginFrame();
}

void miso__renderer_end_frame(void) {
    Renderer_EndFrame();
}

SDL_GPUTexture *miso__renderer_load_texture(const char *path) {
    return Renderer_LoadTexture(path);
}

void miso__renderer_destroy_texture(SDL_GPUTexture *texture) {
    Renderer_DestroyTexture(texture);
}

void miso__renderer_set_view_projection(const float *view_projection) {
    Renderer_SetViewProjection(view_projection);
}

void miso__renderer_set_water_params(float time, float speed, float amplitude, float phase) {
    Renderer_SetWaterParams(time, speed, amplitude, phase);
}

void miso__renderer_draw_sprites(SDL_GPUTexture *texture, const void *instances, int count) {
    Renderer_DrawSprites(texture, (const SpriteInstance *)instances, count);
}

void miso__renderer_draw_native_sprites(void *texture, const void *instances, int count) {
    Renderer_DrawSprites((SDL_GPUTexture *)texture, (const SpriteInstance *)instances, count);
}

void miso__renderer_draw_line_batch(const float *vertices_xyz, const int vertex_count, const SDL_FColor color) {
    Renderer_DrawLineBatch(vertices_xyz, vertex_count, color);
}

void miso__renderer_draw_geometry(const SDL_Vertex *vertices, int count) {
    Renderer_DrawGeometry(vertices, count);
}

void miso__renderer_draw_texture_debug(
    void *texture, const float x, const float y, const float width, const float height) {
    Renderer_DrawTextureDebug((SDL_GPUTexture *)texture, x, y, width, height);
}

bool miso__renderer_copy_frame_stats(MisoRendererFrameStatsSnapshot *out_stats) {
    if (!out_stats) {
        return false;
    }

    const RendererFrameStats *stats = Renderer_GetFrameStats();
    if (!stats) {
        SDL_memset(out_stats, 0, sizeof(*out_stats));
        return false;
    }

    SDL_memcpy(out_stats, stats, sizeof(*out_stats));
    return true;
}

TTF_TextEngine *miso__renderer_get_text_engine(void) {
    return Renderer_GetTextEngine();
}

void miso__renderer_set_present_mode(const int mode) {
    Renderer_SetPresentMode((SDL_GPUPresentMode)mode);
}

int miso__renderer_get_present_mode(void) {
    return (int)Renderer_GetPresentMode();
}

void miso__renderer_set_vsync_acquire_mode(const int mode) {
    Renderer_SetVSyncAcquireMode((RendererVSyncAcquireMode)mode);
}

int miso__renderer_get_vsync_acquire_mode(void) {
    return (int)Renderer_GetVSyncAcquireMode();
}

bool miso__renderer_set_allowed_frames_in_flight(const Uint32 allowed_frames_in_flight) {
    return Renderer_SetAllowedFramesInFlight(allowed_frames_in_flight);
}

Uint32 miso__renderer_get_allowed_frames_in_flight(void) {
    return Renderer_GetAllowedFramesInFlight();
}

void miso__renderer_set_upload_suppressed(const bool enabled) {
    Renderer_SetUploadSuppressed(enabled);
}

bool miso__renderer_get_upload_suppressed(void) {
    return Renderer_GetUploadSuppressed();
}

void miso__renderer_ui_init(void) {
    UI_Init();
}

void miso__renderer_ui_shutdown(void) {
    UI_Shutdown();
}

void miso__renderer_ui_fill_rect(float x, float y, float w, float h, SDL_FColor color) {
    UI_FillRect(x, y, w, h, color);
}

void miso__renderer_ui_text(TTF_Text *text, float x, float y) {
    UI_Text(text, x, y);
}

void miso__renderer_ui_flush(void) {
    UI_Flush();
}

SDL_Window *miso__renderer_get_window(void) {
    return Renderer_GetWindow();
}

void miso__renderer_end_render_pass(void) {
    Renderer_EndRenderPass();
}
