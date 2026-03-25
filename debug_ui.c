#include "debug_ui.h"

// Nuklear configuration - must be before nuklear.h
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_COMMAND_USERDATA
#define NK_IMPLEMENTATION
#include "vendored/nuklear/nuklear.h"

// SDL_GPU backend
#define NK_SDL3_GPU_IMPLEMENTATION
#include "renderer/nuklear_sdl3_gpu.h"
#include "renderer/renderer_internal.h"

#include <SDL3/SDL_log.h>

static struct nk_context *nk_ctx = nullptr;
static bool initialized = false;
static float ui_scale = 1.0f;

bool DebugUI_Init(const char *const font_path, const float font_size) {
    if (initialized)
        return true;

    SDL_GPUDevice *device = Renderer_GetDevice();
    SDL_Window *window = Renderer_GetWindow();

    if (!device || !window) {
        SDL_Log("DebugUI: Renderer not initialized");
        return false;
    }

    nk_ctx = nk_sdl_gpu_init(window, device);
    if (!nk_ctx) {
        SDL_Log("DebugUI: Failed to initialize Nuklear");
        return false;
    }

    // Scale font and UI by pixel density for HDPI displays
    ui_scale = SDL_GetWindowPixelDensity(window);
    const float scaled_font_size = font_size * ui_scale;

    // Load font
    struct nk_font_atlas *const atlas = nk_sdl_gpu_font_stash_begin(nk_ctx);

    // Load TTF font file at scaled size
    struct nk_font *const font = nk_font_atlas_add_from_file(atlas, font_path, scaled_font_size, nullptr);
    if (!font) {
        SDL_Log("DebugUI: Failed to load font from %s, using default", font_path);
    }

    nk_sdl_gpu_font_stash_end(nk_ctx);

    if (font) {
        nk_style_set_font(nk_ctx, &font->handle);
    }

    // Scale all style elements for HDPI
    // This scales padding, spacing, borders, etc.
    struct nk_style *style = &nk_ctx->style;

    // Window
    style->window.header.padding.x *= ui_scale;
    style->window.header.padding.y *= ui_scale;
    style->window.header.label_padding.x *= ui_scale;
    style->window.header.label_padding.y *= ui_scale;
    style->window.header.spacing.x *= ui_scale;
    style->window.header.spacing.y *= ui_scale;
    style->window.spacing.x *= ui_scale;
    style->window.spacing.y *= ui_scale;
    style->window.padding.x *= ui_scale;
    style->window.padding.y *= ui_scale;
    style->window.group_padding.x *= ui_scale;
    style->window.group_padding.y *= ui_scale;
    style->window.border *= ui_scale;
    style->window.group_border *= ui_scale;
    style->window.min_row_height_padding *= ui_scale;

    // Buttons
    style->button.padding.x *= ui_scale;
    style->button.padding.y *= ui_scale;
    style->button.border *= ui_scale;
    style->button.rounding *= ui_scale;

    // Checkbox
    style->checkbox.padding.x *= ui_scale;
    style->checkbox.padding.y *= ui_scale;
    style->checkbox.border *= ui_scale;
    style->checkbox.spacing *= ui_scale;

    // Text
    style->text.padding.x *= ui_scale;
    style->text.padding.y *= ui_scale;

    initialized = true;
    SDL_Log("DebugUI: Initialized with font %s at size %.0f (scaled: %.0f, density: %.1f)",
            font_path,
            font_size,
            scaled_font_size,
            ui_scale);
    return true;
}

void DebugUI_Shutdown(void) {
    if (!initialized)
        return;

    nk_sdl_gpu_shutdown(nk_ctx);
    nk_ctx = nullptr;
    initialized = false;
}

void DebugUI_BeginInput(void) {
    if (!initialized)
        return;
    nk_input_begin(nk_ctx);
}

void DebugUI_EndInput(void) {
    if (!initialized)
        return;
    nk_input_end(nk_ctx);
}

bool DebugUI_HandleEvent(SDL_Event *const evt) {
    if (!initialized)
        return false;
    return nk_sdl_gpu_handle_event(nk_ctx, evt) != 0;
}

struct nk_context *DebugUI_GetContext(void) {
    return nk_ctx;
}

float DebugUI_GetScale(void) {
    return ui_scale;
}

void DebugUI_Render(void) {
    if (!initialized || !nk_ctx)
        return;

    SDL_GPUCommandBuffer *const cmd = Renderer_GetCommandBuffer();
    SDL_GPUTexture *const swapchain = Renderer_GetSwapchainTexture();
    Uint32 swapchain_width = 0;
    Uint32 swapchain_height = 0;
    Renderer_GetSwapchainTextureSize(&swapchain_width, &swapchain_height);

    if (!cmd || !swapchain || swapchain_width == 0U || swapchain_height == 0U) {
        // Still need to clear Nuklear state even if we can't render
        nk_clear(nk_ctx);
        return;
    }

    // End any active render pass before Nuklear renders
    Renderer_EndRenderPass();

    // Render Nuklear UI
    nk_sdl_gpu_render(nk_ctx, cmd, swapchain, swapchain_width, swapchain_height, NK_ANTI_ALIASING_ON);

    // Resume render pass for any subsequent rendering
    Renderer_ResumeRenderPass();
}
