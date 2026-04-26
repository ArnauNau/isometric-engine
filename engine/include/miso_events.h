#ifndef MISO_EVENTS_H
#define MISO_EVENTS_H


typedef enum MisoEventType {
    MISO_EVENT_NONE = 0,
    MISO_EVENT_QUIT,
    MISO_EVENT_WINDOW_RESIZED,
    MISO_EVENT_MOUSE_MOVE,
    MISO_EVENT_MOUSE_BUTTON,
    MISO_EVENT_MOUSE_WHEEL,
    MISO_EVENT_KEY,
    MISO_EVENT_TEXT_INPUT
} MisoEventType;

typedef enum MisoMouseButton {
    MISO_MOUSE_BUTTON_LEFT = 1,
    MISO_MOUSE_BUTTON_MIDDLE = 2,
    MISO_MOUSE_BUTTON_RIGHT = 3,
    MISO_MOUSE_BUTTON_X1 = 4,
    MISO_MOUSE_BUTTON_X2 = 5
} MisoMouseButton;

typedef enum MisoKeyModifiers {
    MISO_KEYMOD_NONE = 0,
    MISO_KEYMOD_SHIFT = 1u << 0,
    MISO_KEYMOD_CTRL = 1u << 1,
    MISO_KEYMOD_ALT = 1u << 2,
    MISO_KEYMOD_GUI = 1u << 3,
    MISO_KEYMOD_CAPS = 1u << 4,
    MISO_KEYMOD_NUM = 1u << 5
} MisoKeyModifiers;

typedef struct MisoMouseMoveEvent {
    int x;
    int y;
    int dx;
    int dy;
} MisoMouseMoveEvent;

typedef struct MisoMouseButtonEvent {
    int x;
    int y;
    MisoMouseButton button;
    bool down;
} MisoMouseButtonEvent;

typedef struct MisoMouseWheelEvent {
    float x;
    float y;
} MisoMouseWheelEvent;

typedef struct MisoKeyEvent {
    int keycode;
    int scancode;
    uint32_t modifiers;
    bool down;
    bool repeat;
} MisoKeyEvent;

typedef struct MisoTextInputEvent {
    char text[32];
} MisoTextInputEvent;

typedef struct MisoWindowResizedEvent {
    int width;
    int height;
} MisoWindowResizedEvent;

typedef struct MisoEvent {
    MisoEventType type;
    union {
        MisoMouseMoveEvent mouse_move;
        MisoMouseButtonEvent mouse_button;
        MisoMouseWheelEvent mouse_wheel;
        MisoKeyEvent key;
        MisoTextInputEvent text_input;
        MisoWindowResizedEvent window_resized;
    } data;
} MisoEvent;

#endif
