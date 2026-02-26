#include "miso_render.h"

#include "internal/miso__engine_internal.h"
#include "internal/miso__renderer_backend.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#define MISO_TEXTURE_TABLE_MAX 4096U
#define MISO_FONT_TABLE_MAX 256U

typedef struct MisoFontEntry {
    TTF_Font *font;
    TTF_Text *text;
} MisoFontEntry;

static SDL_GPUTexture *g_texture_table[MISO_TEXTURE_TABLE_MAX] = {0};
static MisoFontEntry g_font_table[MISO_FONT_TABLE_MAX] = {0};
static SDL_Vertex *g_world_geometry_scratch = nullptr;
static int g_world_geometry_scratch_capacity = 0;

static SDL_FColor miso__color_from_rgba8(const uint32_t rgba8) {
    const float r = (float)((rgba8 >> 24) & 0xFF) / 255.0f;
    const float g = (float)((rgba8 >> 16) & 0xFF) / 255.0f;
    const float b = (float)((rgba8 >> 8) & 0xFF) / 255.0f;
    const float a = (float)(rgba8 & 0xFF) / 255.0f;
    return (SDL_FColor){r, g, b, a};
}

static bool miso__ensure_world_geometry_scratch(const int vertex_count) {
    if (vertex_count <= 0) {
        return false;
    }
    if (vertex_count <= g_world_geometry_scratch_capacity) {
        return true;
    }

    int new_capacity = g_world_geometry_scratch_capacity <= 0 ? 4096 : g_world_geometry_scratch_capacity;
    while (new_capacity < vertex_count) {
        new_capacity *= 2;
    }

    SDL_Vertex *new_scratch = SDL_realloc(g_world_geometry_scratch, sizeof(SDL_Vertex) * (size_t)new_capacity);
    if (!new_scratch) {
        return false;
    }

    g_world_geometry_scratch = new_scratch;
    g_world_geometry_scratch_capacity = new_capacity;
    return true;
}

MisoResult
miso_render_load_texture(const MisoEngine *const engine, const char *const path, MisoTextureHandle *const out_texture) {
    (void)engine;

    if (!path || !out_texture) {
        return MISO_ERR_INVALID_ARG;
    }

    SDL_GPUTexture *texture = miso__renderer_load_texture(path);
    if (!texture) {
        return MISO_ERR_IO;
    }

    for (uint32_t i = 1; i < MISO_TEXTURE_TABLE_MAX; i++) {
        if (!g_texture_table[i]) {
            g_texture_table[i] = texture;
            *out_texture = i;
            return MISO_OK;
        }
    }

    miso__renderer_destroy_texture(texture);
    return MISO_ERR_OUT_OF_MEMORY;
}

void miso_render_destroy_texture(const MisoEngine *const engine, const MisoTextureHandle texture) {
    (void)engine;

    if (texture == 0 || texture >= MISO_TEXTURE_TABLE_MAX || !g_texture_table[texture]) {
        return;
    }

    miso__renderer_destroy_texture(g_texture_table[texture]);
    g_texture_table[texture] = nullptr;
}

MisoResult miso_render_load_font(const MisoEngine *const engine,
                                 const char *const path,
                                 const float point_size,
                                 MisoFontHandle *const out_font) {
    (void)engine;

    if (!path || !out_font || point_size <= 0.0f) {
        return MISO_ERR_INVALID_ARG;
    }

    TTF_TextEngine *text_engine = miso__renderer_get_text_engine();
    if (!text_engine) {
        return MISO_ERR_GPU;
    }

    TTF_Font *font = TTF_OpenFont(path, point_size);
    if (!font) {
        return MISO_ERR_IO;
    }

    TTF_Text *text = TTF_CreateText(text_engine, font, "", 0);
    if (!text) {
        TTF_CloseFont(font);
        return MISO_ERR_GPU;
    }

    for (uint32_t i = 1; i < MISO_FONT_TABLE_MAX; i++) {
        if (!g_font_table[i].font) {
            g_font_table[i].font = font;
            g_font_table[i].text = text;
            *out_font = i;
            return MISO_OK;
        }
    }

    TTF_DestroyText(text);
    TTF_CloseFont(font);
    return MISO_ERR_OUT_OF_MEMORY;
}

void miso_render_destroy_font(const MisoEngine *const engine, const MisoFontHandle font) {
    (void)engine;

    if (font == 0 || font >= MISO_FONT_TABLE_MAX || !g_font_table[font].font) {
        return;
    }

    if (g_font_table[font].text) {
        TTF_DestroyText(g_font_table[font].text);
    }
    TTF_CloseFont(g_font_table[font].font);

    g_font_table[font].text = nullptr;
    g_font_table[font].font = nullptr;
}

bool miso_render_get_frame_stats(const MisoEngine *engine, MisoRenderFrameStats *out_stats) {
    (void)engine;

    if (!out_stats) {
        return false;
    }

    MisoRendererFrameStatsSnapshot snapshot;
    if (!miso__renderer_copy_frame_stats(&snapshot)) {
        SDL_memset(out_stats, 0, sizeof(*out_stats));
        return false;
    }

    SDL_memcpy(out_stats, &snapshot, sizeof(*out_stats));
    return true;
}

void miso_render_begin_world(const MisoEngine *const engine, const MisoCameraId camera_id) {
    if (!engine) {
        return;
    }

    float view_projection[16] = {0};
    miso__camera_get_view_projection(engine, camera_id, view_projection);
    miso__renderer_set_view_projection(view_projection);
}

void miso_render_set_water_params(
    const MisoEngine *const engine, const float time, const float speed, const float amplitude, const float phase) {
    (void)engine;
    miso__renderer_set_water_params(time, speed, amplitude, phase);
}

void miso_render_submit_sprites(const MisoEngine *engine,
                                const MisoTextureHandle texture,
                                const MisoSpriteInstance *instances,
                                const int count) {
    (void)engine;

    if (texture == 0 || texture >= MISO_TEXTURE_TABLE_MAX || !instances || count <= 0 || !g_texture_table[texture]) {
        return;
    }

    miso__renderer_draw_sprites(g_texture_table[texture], instances, count);
}

void miso_render_submit_world_geometry(const MisoEngine *const engine,
                                       const MisoWorldVertex *const vertices,
                                       const int count) {
    (void)engine;

    if (!vertices || count <= 0) {
        return;
    }
    if (!miso__ensure_world_geometry_scratch(count)) {
        return;
    }

    for (int i = 0; i < count; i++) {
        const MisoWorldVertex *in = &vertices[i];
        g_world_geometry_scratch[i] = (SDL_Vertex){
            .position = {in->x, in->y},
            .color = {in->r, in->g, in->b, in->a},
            .tex_coord = {0.0f, 0.0f},
        };
    }

    miso__renderer_draw_geometry(g_world_geometry_scratch, count);
}

void miso_render_end_world(const MisoEngine *engine) {
    (void)engine;
}

void miso_render_begin_ui(const MisoEngine *engine) {
    (void)engine;
}

void miso_render_submit_ui_rect(
    const MisoEngine *const engine, const float x, const float y, const float w, const float h, const uint32_t rgba8) {
    (void)engine;
    miso__renderer_ui_fill_rect(x, y, w, h, miso__color_from_rgba8(rgba8));
}

void miso_render_submit_ui_text(const MisoEngine *const engine,
                                const MisoFontHandle font,
                                const char *const text,
                                const float x,
                                const float y,
                                const uint32_t rgba8) {
    (void)engine;
    (void)rgba8;

    if (font == 0 || font >= MISO_FONT_TABLE_MAX || !text || !g_font_table[font].text) {
        return;
    }
    if (text[0] == '\0') {
        return;
    }

    TTF_SetTextString(g_font_table[font].text, text, 0);
    miso__renderer_ui_text(g_font_table[font].text, x, y);
}

void miso_render_end_ui(const MisoEngine *engine) {
    (void)engine;
    miso__renderer_ui_flush();
}

void miso__render_shutdown(void) {
    for (uint32_t i = 1; i < MISO_TEXTURE_TABLE_MAX; i++) {
        if (g_texture_table[i]) {
            miso__renderer_destroy_texture(g_texture_table[i]);
            g_texture_table[i] = nullptr;
        }
    }

    for (uint32_t i = 1; i < MISO_FONT_TABLE_MAX; i++) {
        if (g_font_table[i].text) {
            TTF_DestroyText(g_font_table[i].text);
        }
        if (g_font_table[i].font) {
            TTF_CloseFont(g_font_table[i].font);
        }
        g_font_table[i].text = nullptr;
        g_font_table[i].font = nullptr;
    }

    SDL_free(g_world_geometry_scratch);
    g_world_geometry_scratch = nullptr;
    g_world_geometry_scratch_capacity = 0;
}
