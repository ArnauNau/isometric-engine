#include "miso_debug_ui.h"

#include "internal/miso__renderer_backend.h"
#include "renderer/renderer_internal.h"

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_COMMAND_USERDATA
#define NK_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-float-conversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif
#include "vendored/nuklear/nuklear.h"

#define NK_SDL3_GPU_IMPLEMENTATION
#include "renderer/nuklear_sdl3_gpu.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <SDL3/SDL.h>

static struct nk_context *context = nullptr;
static bool initialized = false;
static float ui_scale = 1.0f;

static float miso__debug_ui_positive_scale_or_default(const float scale) {
    return scale > 0.0f ? scale : 1.0f;
}

static void miso__debug_ui_apply_style(struct nk_context *const ctx) {
    struct nk_color colors[NK_COLOR_COUNT];
    SDL_memcpy(colors, nk_default_color_style, sizeof(colors));

    colors[NK_COLOR_TEXT] = nk_rgba(235, 238, 240, 255);
    colors[NK_COLOR_WINDOW].a = 178;
    colors[NK_COLOR_HEADER].a = 205;
    colors[NK_COLOR_BUTTON].a = 205;
    colors[NK_COLOR_BUTTON_HOVER].a = 220;
    colors[NK_COLOR_BUTTON_ACTIVE].a = 235;
    colors[NK_COLOR_SELECT].a = 190;
    colors[NK_COLOR_SELECT_ACTIVE].a = 220;
    colors[NK_COLOR_SLIDER].a = 190;
    colors[NK_COLOR_PROPERTY].a = 190;
    colors[NK_COLOR_EDIT].a = 205;
    colors[NK_COLOR_COMBO].a = 205;
    colors[NK_COLOR_SCROLLBAR].a = 160;
    colors[NK_COLOR_TAB_HEADER].a = 205;
    colors[NK_COLOR_KNOB].a = 190;

    nk_style_from_table(ctx, colors);
}

static void miso__debug_ui_scale_style(struct nk_context *const ctx, const float scale) {
    struct nk_style *const style = &ctx->style;

    style->window.header.padding.x *= scale;
    style->window.header.padding.y *= scale;
    style->window.header.label_padding.x *= scale;
    style->window.header.label_padding.y *= scale;
    style->window.header.spacing.x *= scale;
    style->window.header.spacing.y *= scale;
    style->window.spacing.x *= scale;
    style->window.spacing.y *= scale;
    style->window.padding.x *= scale;
    style->window.padding.y *= scale;
    style->window.group_padding.x *= scale;
    style->window.group_padding.y *= scale;
    style->window.border *= scale;
    style->window.group_border *= scale;
    style->window.min_row_height_padding *= scale;

    style->button.padding.x *= scale;
    style->button.padding.y *= scale;
    style->button.border *= scale;
    style->button.rounding *= scale;

    style->checkbox.padding.x *= scale;
    style->checkbox.padding.y *= scale;
    style->checkbox.border *= scale;
    style->checkbox.spacing *= scale;

    style->text.padding.x *= scale;
    style->text.padding.y *= scale;
}

static float miso__debug_ui_current_window_scale(void) {
    SDL_Window *const window = miso__renderer_get_window();
    if (!window) {
        return miso__debug_ui_positive_scale_or_default(ui_scale);
    }

    return miso__debug_ui_positive_scale_or_default(SDL_GetWindowPixelDensity(window));
}

static void miso_debug_ui_sync_modifiers(struct nk_context *const ctx, const uint32_t modifiers) {
    const nk_bool shift = (modifiers & MISO_KEYMOD_SHIFT) != 0;
    const nk_bool ctrl = (modifiers & MISO_KEYMOD_CTRL) != 0;
    nk_input_key(ctx, NK_KEY_SHIFT, shift);
    nk_input_key(ctx, NK_KEY_CTRL, ctrl);
}

static void miso_debug_ui_feed_key(struct nk_context *const ctx, const MisoKeyEvent *const key) {
    const nk_bool down = key->down;
    const bool ctrl = (key->modifiers & MISO_KEYMOD_CTRL) != 0;

    miso_debug_ui_sync_modifiers(ctx, key->modifiers);

    switch (key->keycode) {
    case SDLK_DELETE:
        nk_input_key(ctx, NK_KEY_DEL, down);
        break;
    case SDLK_RETURN:
        nk_input_key(ctx, NK_KEY_ENTER, down);
        break;
    case SDLK_TAB:
        nk_input_key(ctx, NK_KEY_TAB, down);
        break;
    case SDLK_BACKSPACE:
        nk_input_key(ctx, NK_KEY_BACKSPACE, down);
        break;
    case SDLK_HOME:
        nk_input_key(ctx, NK_KEY_TEXT_START, down);
        nk_input_key(ctx, NK_KEY_SCROLL_START, down);
        break;
    case SDLK_END:
        nk_input_key(ctx, NK_KEY_TEXT_END, down);
        nk_input_key(ctx, NK_KEY_SCROLL_END, down);
        break;
    case SDLK_PAGEDOWN:
        nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down);
        break;
    case SDLK_PAGEUP:
        nk_input_key(ctx, NK_KEY_SCROLL_UP, down);
        break;
    case SDLK_Z:
        nk_input_key(ctx, NK_KEY_TEXT_UNDO, (nk_bool)(down && ctrl));
        break;
    case SDLK_R:
        nk_input_key(ctx, NK_KEY_TEXT_REDO, (nk_bool)(down && ctrl));
        break;
    case SDLK_C:
        nk_input_key(ctx, NK_KEY_COPY, (nk_bool)(down && ctrl));
        break;
    case SDLK_V:
        nk_input_key(ctx, NK_KEY_PASTE, (nk_bool)(down && ctrl));
        break;
    case SDLK_X:
        nk_input_key(ctx, NK_KEY_CUT, (nk_bool)(down && ctrl));
        break;
    case SDLK_B:
        nk_input_key(ctx, NK_KEY_TEXT_LINE_START, (nk_bool)(down && ctrl));
        break;
    case SDLK_E:
        nk_input_key(ctx, NK_KEY_TEXT_LINE_END, (nk_bool)(down && ctrl));
        break;
    case SDLK_UP:
        nk_input_key(ctx, NK_KEY_UP, down);
        break;
    case SDLK_DOWN:
        nk_input_key(ctx, NK_KEY_DOWN, down);
        break;
    case SDLK_LEFT:
        nk_input_key(ctx, ctrl ? NK_KEY_TEXT_WORD_LEFT : NK_KEY_LEFT, down);
        break;
    case SDLK_RIGHT:
        nk_input_key(ctx, ctrl ? NK_KEY_TEXT_WORD_RIGHT : NK_KEY_RIGHT, down);
        break;
    default:
        break;
    }
}

static void miso_debug_ui_feed_text(struct nk_context *const ctx, const MisoTextInputEvent *const text_input) {
    const char *cursor = text_input->text;
    size_t remaining = SDL_strlen(text_input->text);
    while (remaining > 0) {
        const Uint32 codepoint = SDL_StepUTF8(&cursor, &remaining);
        if (codepoint != 0) {
            nk_input_unicode(ctx, (nk_rune)codepoint);
        }
    }
}

MisoResult miso_debug_ui_init(const MisoEngine *const engine, const char *const font_path, const float font_size) {
    (void)engine;
    if (initialized) {
        return MISO_OK;
    }

    SDL_GPUDevice *const device = Renderer_GetDevice();
    SDL_Window *const window = Renderer_GetWindow();
    if (!device || !window) {
        SDL_Log("miso_debug_ui: renderer is not initialized");
        return MISO_ERR_INIT;
    }

    context = nk_sdl_gpu_init(window, device);
    if (!context) {
        SDL_Log("miso_debug_ui: failed to initialize Nuklear");
        return MISO_ERR_INIT;
    }

    ui_scale = miso__debug_ui_positive_scale_or_default(SDL_GetWindowPixelDensity(window));
    const float scaled_font_size = font_size * ui_scale;

    struct nk_font_atlas *const atlas = nk_sdl_gpu_font_stash_begin(context);
    struct nk_font *const font = nk_font_atlas_add_from_file(atlas, font_path, scaled_font_size, nullptr);
    if (!font) {
        SDL_Log("miso_debug_ui: failed to load font from %s, using default", font_path ? font_path : "(null)");
    }

    nk_sdl_gpu_font_stash_end(context);
    if (font) {
        nk_style_set_font(context, &font->handle);
    }

    miso__debug_ui_apply_style(context);
    miso__debug_ui_scale_style(context, ui_scale);
    initialized = true;

    SDL_Log("miso_debug_ui: initialized with font %s at %.0f px (scaled %.0f px, density %.1f)",
            font_path ? font_path : "(null)",
            font_size,
            scaled_font_size,
            ui_scale);
    return MISO_OK;
}

void miso_debug_ui_shutdown(void) {
    if (!initialized) {
        return;
    }

    nk_sdl_gpu_shutdown(context);
    context = nullptr;
    initialized = false;
    ui_scale = 1.0f;
}

void miso_debug_ui_begin_input(void) {
    if (initialized) {
        nk_input_begin(context);
    }
}

void miso_debug_ui_end_input(void) {
    if (initialized) {
        nk_input_end(context);
    }
}

bool miso_debug_ui_feed_event(const MisoEvent *const event) {
    if (!event || !context) {
        return false;
    }

    const float scale = miso__debug_ui_current_window_scale();

    switch (event->type) {
    case MISO_EVENT_MOUSE_MOVE:
        nk_input_motion(context,
                        (int)SDL_lroundf((float)event->data.mouse_move.x * scale),
                        (int)SDL_lroundf((float)event->data.mouse_move.y * scale));
        break;
    case MISO_EVENT_MOUSE_BUTTON: {
        enum nk_buttons button = NK_BUTTON_LEFT;
        bool supported = true;
        switch (event->data.mouse_button.button) {
        case MISO_MOUSE_BUTTON_LEFT:
            button = NK_BUTTON_LEFT;
            break;
        case MISO_MOUSE_BUTTON_MIDDLE:
            button = NK_BUTTON_MIDDLE;
            break;
        case MISO_MOUSE_BUTTON_RIGHT:
            button = NK_BUTTON_RIGHT;
            break;
        default:
            supported = false;
            break;
        }
        if (supported) {
            nk_input_button(context,
                            button,
                            (int)SDL_lroundf((float)event->data.mouse_button.x * scale),
                            (int)SDL_lroundf((float)event->data.mouse_button.y * scale),
                            (nk_bool)event->data.mouse_button.down);
        }
        break;
    }
    case MISO_EVENT_MOUSE_WHEEL:
        nk_input_scroll(context, (struct nk_vec2){event->data.mouse_wheel.x, event->data.mouse_wheel.y});
        break;
    case MISO_EVENT_KEY:
        miso_debug_ui_feed_key(context, &event->data.key);
        break;
    case MISO_EVENT_TEXT_INPUT:
        miso_debug_ui_feed_text(context, &event->data.text_input);
        break;
    default:
        break;
    }

    return nk_item_is_any_active(context) || nk_window_is_any_hovered(context);
}

void miso_debug_ui_prepare_render(const MisoEngine *const engine) {
    (void)engine;
    miso__renderer_end_render_pass();
}

struct nk_context *miso_debug_ui_get_context(void) {
    return context;
}

float miso_debug_ui_get_scale(void) {
    return ui_scale;
}

void miso_debug_ui_render(const MisoEngine *const engine) {
    (void)engine;
    if (!initialized || !context) {
        return;
    }

    SDL_GPUCommandBuffer *const cmd = Renderer_GetCommandBuffer();
    SDL_GPUTexture *const swapchain = Renderer_GetSwapchainTexture();
    Uint32 swapchain_width = 0;
    Uint32 swapchain_height = 0;
    Renderer_GetSwapchainTextureSize(&swapchain_width, &swapchain_height);

    if (!cmd || !swapchain || swapchain_width == 0U || swapchain_height == 0U) {
        nk_clear(context);
        return;
    }

    Renderer_EndRenderPass();
    nk_sdl_gpu_render(context, cmd, swapchain, swapchain_width, swapchain_height, NK_ANTI_ALIASING_ON);
}
