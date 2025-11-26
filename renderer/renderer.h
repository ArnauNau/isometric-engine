#ifndef RENDERER_H
#define RENDERER_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>

typedef struct {
  float x, y, z, p1;
  float w, h, p2, p3;
  float u, v, uw, vh;
} SpriteInstance;

bool Renderer_Init(SDL_Window *window);
void Renderer_Shutdown(void);
void Renderer_Resize(int width, int height);
void Renderer_SetVSync(bool enabled);

// Uploads a texture to the GPU and returns a handle (wrapper or index)
// For simplicity, we'll return the SDL_GPUTexture* directly for now,
// but in a real engine you'd want a resource handle.
SDL_GPUTexture *Renderer_LoadTexture(const char *path);
void Renderer_DestroyTexture(SDL_GPUTexture *texture);

void Renderer_BeginFrame(void);
void Renderer_EndFrame(void);

// Update the camera/view projection
void Renderer_SetViewProjection(const float *viewProjMatrix);

// Draw a batch of sprites
void Renderer_DrawSprites(SDL_GPUTexture *texture,
                          const SpriteInstance *instances, int count);

// Update the camera/view projection
void Renderer_DrawLine(float x1, float y1, float z1, float x2, float y2,
                       float z2, SDL_FColor color);
void Renderer_DrawGeometry(const SDL_Vertex *vertices, int count);

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

// Flush screen-space geometry (single draw call)
void Renderer_FlushUIGeometry(const SDL_Vertex *vertices, int count);

// Flush screen-space text (one draw call per atlas)
void Renderer_FlushUIText(const float *vertices, int vertex_count,
                          const int *indices, int index_count,
                          const UITextAtlasInfo *atlases, int atlas_count);


/* ------------------ DEBUG UTILITIES ------------------ */
// Debug: Draw a texture as a screen-space quad
void Renderer_DrawTextureDebug(SDL_GPUTexture *texture, float x, float y,
                               float width, float height);

// Debug: Draw a filled colored quad using geometry pipeline (to verify rendering works)
void Renderer_DrawFilledQuadDebug(float x, float y, float width, float height,
                                  SDL_FColor color);

void Renderer_SetPresentMode(SDL_GPUPresentMode mode);

#endif // RENDERER_H
