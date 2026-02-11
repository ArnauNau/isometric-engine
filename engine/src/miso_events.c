#include "internal/miso__engine_internal.h"
#include "internal/miso__renderer_backend.h"
#include "miso_engine.h"

#include <SDL3/SDL.h>

static MisoMouseButton miso__to_mouse_button(const uint8_t button) {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return MISO_MOUSE_BUTTON_LEFT;
    case SDL_BUTTON_MIDDLE:
        return MISO_MOUSE_BUTTON_MIDDLE;
    case SDL_BUTTON_RIGHT:
        return MISO_MOUSE_BUTTON_RIGHT;
    case SDL_BUTTON_X1:
        return MISO_MOUSE_BUTTON_X1;
    case SDL_BUTTON_X2:
        return MISO_MOUSE_BUTTON_X2;
    default:
        return MISO_MOUSE_BUTTON_LEFT;
    }
}

static uint32_t miso__to_key_modifiers(const SDL_Keymod mod) {
    uint32_t flags = MISO_KEYMOD_NONE;
    if (mod & SDL_KMOD_SHIFT) {
        flags |= MISO_KEYMOD_SHIFT;
    }
    if (mod & SDL_KMOD_CTRL) {
        flags |= MISO_KEYMOD_CTRL;
    }
    if (mod & SDL_KMOD_ALT) {
        flags |= MISO_KEYMOD_ALT;
    }
    if (mod & SDL_KMOD_GUI) {
        flags |= MISO_KEYMOD_GUI;
    }
    if (mod & SDL_KMOD_CAPS) {
        flags |= MISO_KEYMOD_CAPS;
    }
    if (mod & SDL_KMOD_NUM) {
        flags |= MISO_KEYMOD_NUM;
    }
    return flags;
}

bool miso_poll_event(MisoEngine *engine, MisoEvent *out_event) {
    if (!engine || !out_event) {
        return false;
    }

    SDL_Event event;
    if (!SDL_PollEvent(&event)) {
        return false;
    }

    SDL_memset(out_event, 0, sizeof(*out_event));
    out_event->type = MISO_EVENT_NONE;

    switch (event.type) {
    case SDL_EVENT_QUIT:
        out_event->type = MISO_EVENT_QUIT;
        miso__engine_request_quit(engine);
        break;

    case SDL_EVENT_WINDOW_RESIZED:
        out_event->type = MISO_EVENT_WINDOW_RESIZED;
        out_event->data.window_resized.width = event.window.data1;
        out_event->data.window_resized.height = event.window.data2;
        miso__renderer_resize(event.window.data1, event.window.data2);
        break;

    case SDL_EVENT_MOUSE_MOTION:
        out_event->type = MISO_EVENT_MOUSE_MOVE;
        out_event->data.mouse_move.x = (int)SDL_lroundf(event.motion.x);
        out_event->data.mouse_move.y = (int)SDL_lroundf(event.motion.y);
        out_event->data.mouse_move.dx = (int)SDL_lroundf(event.motion.xrel);
        out_event->data.mouse_move.dy = (int)SDL_lroundf(event.motion.yrel);
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        out_event->type = MISO_EVENT_MOUSE_BUTTON;
        out_event->data.mouse_button.x = (int)SDL_lroundf(event.button.x);
        out_event->data.mouse_button.y = (int)SDL_lroundf(event.button.y);
        out_event->data.mouse_button.button = miso__to_mouse_button(event.button.button);
        out_event->data.mouse_button.down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        out_event->type = MISO_EVENT_MOUSE_WHEEL;
        out_event->data.mouse_wheel.x = event.wheel.x;
        out_event->data.mouse_wheel.y = event.wheel.y;
        break;

    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        out_event->type = MISO_EVENT_KEY;
        out_event->data.key.keycode = (int)event.key.key;
        out_event->data.key.scancode = (int)event.key.scancode;
        out_event->data.key.modifiers = miso__to_key_modifiers(event.key.mod);
        out_event->data.key.down = event.type == SDL_EVENT_KEY_DOWN;
        out_event->data.key.repeat = event.key.repeat;
        break;

    case SDL_EVENT_TEXT_INPUT:
        out_event->type = MISO_EVENT_TEXT_INPUT;
        SDL_strlcpy(out_event->data.text_input.text, event.text.text, sizeof(out_event->data.text_input.text));
        break;

    default:
        return false;
    }

    if (engine->game_registered && engine->game_hooks.on_event) {
        engine->game_hooks.on_event(engine->game_ctx, out_event);
    }

    return true;
}
