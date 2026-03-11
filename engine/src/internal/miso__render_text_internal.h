#ifndef MISO__RENDER_TEXT_INTERNAL_H
#define MISO__RENDER_TEXT_INTERNAL_H

#include "miso_render.h"

#include <SDL3_ttf/SDL_ttf.h>

TTF_Font *miso__render_get_font_ptr(MisoFontHandle font);
void miso__text_on_font_destroyed(MisoFontHandle font);
void miso__text_shutdown(void);

#endif
