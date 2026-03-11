#include "miso_text.h"

#include "internal/miso__render_text_internal.h"
#include "internal/miso__renderer_backend.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#define MISO_TEXT_TABLE_MAX 512U
#define MISO_TEXT_MIN_STRING_CAPACITY 32U

typedef struct MisoTextEntry {
    bool in_use;
    MisoFontHandle font;
    TTF_Text *text;
    char *string;
    uint32_t string_capacity;
    float width;
    float height;
    bool background_enabled;
    uint32_t background_rgba8;
    float background_padding;
} MisoTextEntry;

static MisoTextEntry g_text_table[MISO_TEXT_TABLE_MAX] = {0};

static SDL_FColor miso__text_color_from_rgba8(const uint32_t rgba8) {
    const float r = (float)((rgba8 >> 24) & 0xFFu) / 255.0f;
    const float g = (float)((rgba8 >> 16) & 0xFFu) / 255.0f;
    const float b = (float)((rgba8 >> 8) & 0xFFu) / 255.0f;
    const float a = (float)(rgba8 & 0xFFu) / 255.0f;
    return (SDL_FColor){r, g, b, a};
}

static MisoTextEntry *miso__text_get_entry(const MisoTextHandle handle) {
    if (handle == 0U || handle >= MISO_TEXT_TABLE_MAX) {
        return nullptr;
    }

    MisoTextEntry *entry = &g_text_table[handle];
    return entry->in_use ? entry : nullptr;
}

static void miso__text_refresh_metrics(MisoTextEntry *const entry) {
    if (!entry || !entry->text) {
        return;
    }

    int width = 0;
    int height = 0;
    if (!TTF_GetTextSize(entry->text, &width, &height)) {
        width = 0;
        height = 0;
    }

    entry->width = (float)width;
    entry->height = (float)height;
}

static bool miso__text_ensure_capacity(MisoTextEntry *const entry, const size_t string_len) {
    if (!entry) {
        return false;
    }

    const size_t needed_capacity = string_len + 1U;
    if (needed_capacity <= entry->string_capacity) {
        return true;
    }

    uint32_t new_capacity =
        entry->string_capacity > 0U ? entry->string_capacity : (uint32_t)MISO_TEXT_MIN_STRING_CAPACITY;
    while ((size_t)new_capacity < needed_capacity) {
        new_capacity *= 2U;
    }

    char *new_buffer = SDL_realloc(entry->string, (size_t)new_capacity);
    if (!new_buffer) {
        return false;
    }

    entry->string = new_buffer;
    entry->string_capacity = new_capacity;
    return true;
}

static MisoResult miso__text_update_string(MisoTextEntry *const entry, const char *const string) {
    if (!entry || !entry->text || !string) {
        return MISO_ERR_INVALID_ARG;
    }

    if (entry->string && SDL_strcmp(entry->string, string) == 0) {
        return MISO_OK;
    }

    const size_t string_len = SDL_strlen(string);
    if (!miso__text_ensure_capacity(entry, string_len)) {
        return MISO_ERR_OUT_OF_MEMORY;
    }

    SDL_memcpy(entry->string, string, string_len + 1U);
    if (!TTF_SetTextString(entry->text, entry->string, 0)) {
        return MISO_ERR_GPU;
    }

    miso__text_refresh_metrics(entry);
    return MISO_OK;
}

static void miso__text_release_entry(MisoTextEntry *const entry) {
    if (!entry || !entry->in_use) {
        return;
    }

    if (entry->text) {
        TTF_DestroyText(entry->text);
    }
    SDL_free(entry->string);
    SDL_memset(entry, 0, sizeof(*entry));
}

MisoResult miso_text_create(const MisoEngine *const engine,
                            const MisoFontHandle font,
                            const char *const initial_text,
                            MisoTextHandle *const out_text) {
    (void)engine;

    if (!out_text) {
        return MISO_ERR_INVALID_ARG;
    }

    TTF_Font *font_ptr = miso__render_get_font_ptr(font);
    if (!font_ptr) {
        return MISO_ERR_NOT_FOUND;
    }

    TTF_TextEngine *text_engine = miso__renderer_get_text_engine();
    if (!text_engine) {
        return MISO_ERR_GPU;
    }

    for (uint32_t i = 1U; i < MISO_TEXT_TABLE_MAX; i++) {
        MisoTextEntry *entry = &g_text_table[i];
        if (entry->in_use) {
            continue;
        }

        TTF_Text *text = TTF_CreateText(text_engine, font_ptr, "", 0);
        if (!text) {
            return MISO_ERR_GPU;
        }

        entry->in_use = true;
        entry->font = font;
        entry->text = text;
        entry->background_enabled = false;
        entry->background_rgba8 = 0x00000099u;
        entry->background_padding = 0.0f;

        const MisoResult set_result = miso__text_update_string(entry, initial_text ? initial_text : "");
        if (set_result != MISO_OK) {
            miso__text_release_entry(entry);
            return set_result;
        }

        *out_text = i;
        return MISO_OK;
    }

    return MISO_ERR_OUT_OF_MEMORY;
}

void miso_text_destroy(const MisoEngine *const engine, const MisoTextHandle text) {
    (void)engine;
    miso__text_release_entry(miso__text_get_entry(text));
}

bool miso_text_is_valid(const MisoEngine *const engine, const MisoTextHandle text) {
    (void)engine;
    return miso__text_get_entry(text) != nullptr;
}

MisoResult miso_text_set_string(const MisoEngine *const engine, const MisoTextHandle text, const char *const string) {
    (void)engine;

    MisoTextEntry *entry = miso__text_get_entry(text);
    if (!entry) {
        return MISO_ERR_NOT_FOUND;
    }

    return miso__text_update_string(entry, string ? string : "");
}

bool miso_text_get_metrics(const MisoEngine *const engine,
                           const MisoTextHandle text,
                           MisoTextMetrics *const out_metrics) {
    (void)engine;

    MisoTextEntry *entry = miso__text_get_entry(text);
    if (!entry || !out_metrics) {
        return false;
    }

    out_metrics->width = entry->width;
    out_metrics->height = entry->height;
    return true;
}

void miso_text_set_background_enabled(const MisoEngine *const engine, const MisoTextHandle text, const bool enabled) {
    (void)engine;

    MisoTextEntry *entry = miso__text_get_entry(text);
    if (!entry) {
        return;
    }

    entry->background_enabled = enabled;
}

void miso_text_set_background_style(const MisoEngine *const engine,
                                    const MisoTextHandle text,
                                    const uint32_t rgba8,
                                    const float padding) {
    (void)engine;

    MisoTextEntry *entry = miso__text_get_entry(text);
    if (!entry) {
        return;
    }

    entry->background_rgba8 = rgba8;
    entry->background_padding = padding >= 0.0f ? padding : 0.0f;
}

void miso_render_submit_ui_text_handle(
    const MisoEngine *const engine, const MisoTextHandle text, const float x, const float y, const uint32_t rgba8) {
    (void)engine;
    (void)rgba8;

    MisoTextEntry *entry = miso__text_get_entry(text);
    if (!entry || !entry->text) {
        return;
    }

    if (entry->background_enabled) {
        const float padding = entry->background_padding;
        miso__renderer_ui_fill_rect(x - padding,
                                    y - padding,
                                    entry->width + padding * 2.0f,
                                    entry->height + padding * 2.0f,
                                    miso__text_color_from_rgba8(entry->background_rgba8));
    }

    miso__renderer_ui_text(entry->text, x, y);
}

void miso__text_on_font_destroyed(const MisoFontHandle font) {
    if (font == 0U) {
        return;
    }

    for (uint32_t i = 1U; i < MISO_TEXT_TABLE_MAX; i++) {
        MisoTextEntry *entry = &g_text_table[i];
        if (entry->in_use && entry->font == font) {
            miso__text_release_entry(entry);
        }
    }
}

void miso__text_shutdown(void) {
    for (uint32_t i = 1U; i < MISO_TEXT_TABLE_MAX; i++) {
        miso__text_release_entry(&g_text_table[i]);
    }
}
