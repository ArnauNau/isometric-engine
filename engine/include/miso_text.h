#ifndef MISO_TEXT_H
#define MISO_TEXT_H

#include "miso_engine.h"
#include "miso_render.h"

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t MisoTextHandle;

typedef struct MisoTextMetrics {
    float width;
    float height;
} MisoTextMetrics;

MisoResult
miso_text_create(const MisoEngine *engine, MisoFontHandle font, const char *initial_text, MisoTextHandle *out_text);
void miso_text_destroy(const MisoEngine *engine, MisoTextHandle text);
bool miso_text_is_valid(const MisoEngine *engine, MisoTextHandle text);
MisoResult miso_text_set_string(const MisoEngine *engine, MisoTextHandle text, const char *string);
bool miso_text_get_metrics(const MisoEngine *engine, MisoTextHandle text, MisoTextMetrics *out_metrics);

void miso_text_set_background_enabled(const MisoEngine *engine, MisoTextHandle text, bool enabled);
void miso_text_set_background_style(const MisoEngine *engine, MisoTextHandle text, uint32_t rgba8, float padding);

void miso_render_submit_ui_text_handle(const MisoEngine *engine, MisoTextHandle text, float x, float y, uint32_t rgba8);

#endif
