#ifndef MISO_DEBUG_UI_H
#define MISO_DEBUG_UI_H

#include "miso_engine.h"
#include "miso_events.h"

struct nk_context;

MisoResult miso_debug_ui_init(const MisoEngine *engine, const char *font_path, float font_size);
void miso_debug_ui_shutdown(void);

void miso_debug_ui_begin_input(void);
void miso_debug_ui_end_input(void);
bool miso_debug_ui_feed_event(const MisoEvent *event);
void miso_debug_ui_prepare_render(const MisoEngine *engine);

struct nk_context *miso_debug_ui_get_context(void);
float miso_debug_ui_get_scale(void);
void miso_debug_ui_render(const MisoEngine *engine);

#endif
