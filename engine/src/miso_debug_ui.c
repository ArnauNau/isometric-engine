#include "miso_debug_ui.h"

#include "debug_ui.h"
#include "internal/miso__renderer_backend.h"

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_COMMAND_USERDATA
#include "vendored/nuklear/nuklear.h"

#include <SDL3/SDL.h>

static void miso_debug_ui_sync_modifiers(struct nk_context *ctx, const uint32_t modifiers) {
    const nk_bool shift = (modifiers & MISO_KEYMOD_SHIFT) != 0;
    const nk_bool ctrl = (modifiers & MISO_KEYMOD_CTRL) != 0;
    nk_input_key(ctx, NK_KEY_SHIFT, shift);
    nk_input_key(ctx, NK_KEY_CTRL, ctrl);
}

static void miso_debug_ui_feed_key(struct nk_context *ctx, const MisoKeyEvent *key) {
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
        if (ctrl) {
            nk_input_key(ctx, NK_KEY_TEXT_WORD_LEFT, down);
        } else {
            nk_input_key(ctx, NK_KEY_LEFT, down);
        }
        break;
    case SDLK_RIGHT:
        if (ctrl) {
            nk_input_key(ctx, NK_KEY_TEXT_WORD_RIGHT, down);
        } else {
            nk_input_key(ctx, NK_KEY_RIGHT, down);
        }
        break;
    default:
        break;
    }
}

static void miso_debug_ui_feed_text(struct nk_context *ctx, const MisoTextInputEvent *text_input) {
    const char *cursor = text_input->text;
    size_t remaining = SDL_strlen(text_input->text);
    while (remaining > 0) {
        const Uint32 codepoint = SDL_StepUTF8(&cursor, &remaining);
        if (codepoint != 0) {
            nk_input_unicode(ctx, (nk_rune)codepoint);
        }
    }
}

static float miso_debug_ui_get_window_scale(void) {
    SDL_Window *window = miso__renderer_get_window();
    if (!window) {
        const float fallback = DebugUI_GetScale();
        return fallback > 0.0f ? fallback : 1.0f;
    }

    const float scale = SDL_GetWindowPixelDensity(window);
    if (scale > 0.0f) {
        return scale;
    }

    const float fallback = DebugUI_GetScale();
    return fallback > 0.0f ? fallback : 1.0f;
}

MisoResult miso_debug_ui_init(const MisoEngine *engine, const char *font_path, const float font_size) {
    (void)engine;
    return DebugUI_Init(font_path, font_size) ? MISO_OK : MISO_ERR_INIT;
}

void miso_debug_ui_shutdown(void) {
    DebugUI_Shutdown();
}

void miso_debug_ui_begin_input(void) {
    DebugUI_BeginInput();
}

void miso_debug_ui_end_input(void) {
    DebugUI_EndInput();
}

bool miso_debug_ui_feed_event(const MisoEvent *event) {
    if (!event) {
        return false;
    }

    struct nk_context *ctx = DebugUI_GetContext();
    if (!ctx) {
        return false;
    }

    const float scale = miso_debug_ui_get_window_scale();

    switch (event->type) {
    case MISO_EVENT_MOUSE_MOVE:
        nk_input_motion(ctx,
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
            nk_input_button(ctx,
                            button,
                            (int)SDL_lroundf((float)event->data.mouse_button.x * scale),
                            (int)SDL_lroundf((float)event->data.mouse_button.y * scale),
                            (nk_bool)event->data.mouse_button.down);
        }
        break;
    }
    case MISO_EVENT_MOUSE_WHEEL:
        nk_input_scroll(ctx, (struct nk_vec2){event->data.mouse_wheel.x, event->data.mouse_wheel.y});
        break;
    case MISO_EVENT_KEY:
        miso_debug_ui_feed_key(ctx, &event->data.key);
        break;
    case MISO_EVENT_TEXT_INPUT:
        miso_debug_ui_feed_text(ctx, &event->data.text_input);
        break;
    default:
        break;
    }

    return nk_item_is_any_active(ctx) || nk_window_is_any_hovered(ctx);
}

void miso_debug_ui_prepare_render(const MisoEngine *engine) {
    (void)engine;
    miso__renderer_end_render_pass();
}

struct nk_context *miso_debug_ui_get_context(void) {
    return DebugUI_GetContext();
}

float miso_debug_ui_get_scale(void) {
    return DebugUI_GetScale();
}

void miso_debug_ui_render(const MisoEngine *engine) {
    (void)engine;
    DebugUI_Render();
}
