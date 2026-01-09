#ifndef RENDERER_INTERNAL_H
#define RENDERER_INTERNAL_H

#include <SDL3/SDL.h>

// Internal functions for renderer subsystems (debug_ui, etc.)
// Do not include this header in main.c

SDL_Window *Renderer_GetWindow(void);
SDL_GPUDevice *Renderer_GetDevice(void);
SDL_GPUCommandBuffer *Renderer_GetCommandBuffer(void);
SDL_GPUTexture *Renderer_GetSwapchainTexture(void);

// Render pass management for external rendering
// Call EndRenderPass before external GPU rendering, ResumeRenderPass after
void Renderer_EndRenderPass(void);
void Renderer_ResumeRenderPass(void);

#endif // RENDERER_INTERNAL_H
