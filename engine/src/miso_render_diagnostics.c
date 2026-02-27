#include "miso_render_diagnostics.h"

#include "internal/miso__renderer_backend.h"

#include <SDL3/SDL.h>

static MisoRenderPresentMode miso__present_mode_from_backend(const int mode) {
    switch ((SDL_GPUPresentMode)mode) {
    case SDL_GPU_PRESENTMODE_IMMEDIATE:
        return MISO_RENDER_PRESENT_IMMEDIATE;
    case SDL_GPU_PRESENTMODE_MAILBOX:
        return MISO_RENDER_PRESENT_MAILBOX;
    case SDL_GPU_PRESENTMODE_VSYNC:
    default:
        return MISO_RENDER_PRESENT_VSYNC;
    }
}

static int miso__present_mode_to_backend(const MisoRenderPresentMode mode) {
    switch (mode) {
    case MISO_RENDER_PRESENT_IMMEDIATE:
        return (int)SDL_GPU_PRESENTMODE_IMMEDIATE;
    case MISO_RENDER_PRESENT_MAILBOX:
        return (int)SDL_GPU_PRESENTMODE_MAILBOX;
    case MISO_RENDER_PRESENT_VSYNC:
    default:
        return (int)SDL_GPU_PRESENTMODE_VSYNC;
    }
}

static MisoRenderVSyncAcquireMode miso__vsync_acquire_mode_from_backend(const int mode) {
    return mode == 1 ? MISO_RENDER_VSYNC_ACQUIRE_PASSTHROUGH : MISO_RENDER_VSYNC_ACQUIRE_BLOCKING;
}

static int miso__vsync_acquire_mode_to_backend(const MisoRenderVSyncAcquireMode mode) {
    return mode == MISO_RENDER_VSYNC_ACQUIRE_PASSTHROUGH ? 1 : 0;
}

bool miso_render_diag_get_frame_stats(const MisoEngine *const engine, MisoRenderFrameStats *const out_stats) {
    (void)engine;

    if (!out_stats) {
        return false;
    }

    MisoRendererFrameStatsSnapshot snapshot = {0};
    if (!miso__renderer_copy_frame_stats(&snapshot)) {
        SDL_memset(out_stats, 0, sizeof(*out_stats));
        return false;
    }

    SDL_memcpy(out_stats, &snapshot, sizeof(*out_stats));
    return true;
}

MisoRenderPresentMode miso_render_diag_get_present_mode(const MisoEngine *const engine) {
    (void)engine;
    return miso__present_mode_from_backend(miso__renderer_get_present_mode());
}

MisoRenderVSyncAcquireMode miso_render_diag_get_vsync_acquire_mode(const MisoEngine *const engine) {
    (void)engine;
    return miso__vsync_acquire_mode_from_backend(miso__renderer_get_vsync_acquire_mode());
}

uint32_t miso_render_diag_get_allowed_frames_in_flight(const MisoEngine *const engine) {
    (void)engine;
    return (uint32_t)miso__renderer_get_allowed_frames_in_flight();
}

bool miso_render_diag_get_upload_suppressed(const MisoEngine *const engine) {
    (void)engine;
    return miso__renderer_get_upload_suppressed();
}

void miso_render_tune_set_present_mode(const MisoEngine *const engine, const MisoRenderPresentMode mode) {
    (void)engine;
    miso__renderer_set_present_mode(miso__present_mode_to_backend(mode));
}

void miso_render_tune_set_vsync_acquire_mode(const MisoEngine *const engine, const MisoRenderVSyncAcquireMode mode) {
    (void)engine;
    miso__renderer_set_vsync_acquire_mode(miso__vsync_acquire_mode_to_backend(mode));
}

bool miso_render_tune_set_allowed_frames_in_flight(const MisoEngine *const engine,
                                                   const uint32_t allowed_frames_in_flight) {
    (void)engine;
    return miso__renderer_set_allowed_frames_in_flight((Uint32)allowed_frames_in_flight);
}

void miso_render_tune_set_upload_suppressed(const MisoEngine *const engine, const bool enabled) {
    (void)engine;
    miso__renderer_set_upload_suppressed(enabled);
}

void miso_render_diag_submit_ui_texture_debug(const MisoEngine *const engine,
                                              void *const native_texture,
                                              const float x,
                                              const float y,
                                              const float width,
                                              const float height) {
    (void)engine;
    if (!native_texture) {
        return;
    }
    miso__renderer_draw_texture_debug(native_texture, x, y, width, height);
}

void miso_render_diag_submit_native_sprites(const MisoEngine *const engine,
                                            void *const native_texture,
                                            const MisoSpriteInstance *const instances,
                                            const int count) {
    (void)engine;
    if (!native_texture || !instances || count <= 0) {
        return;
    }
    miso__renderer_draw_native_sprites(native_texture, instances, count);
}
