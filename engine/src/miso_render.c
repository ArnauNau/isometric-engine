#include "miso_render.h"

#include "internal/miso__engine_internal.h"
#include "internal/miso__render_text_internal.h"
#include "internal/miso__renderer_backend.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#define MISO_TEXTURE_TABLE_MAX 4096U
#define MISO_FONT_TABLE_MAX 256U

typedef struct MisoFontEntry {
    TTF_Font *font;
    TTF_Text *text;
} MisoFontEntry;

typedef struct MisoTextureEntry {
    /**
     * Private storage behind MisoTextureHandle.
     *
     * The native texture is renderer-owned through this table entry. Dimensions
     * are retained as lightweight resource metadata for systems such as tile
     * atlases without exposing SDL_GPUTexture.
     */
    SDL_GPUTexture *texture;
    Uint32 width;
    Uint32 height;
} MisoTextureEntry;

static MisoTextureEntry g_texture_table[MISO_TEXTURE_TABLE_MAX] = {0};
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

    SDL_Vertex *const new_scratch = SDL_realloc(g_world_geometry_scratch, sizeof(SDL_Vertex) * (size_t)new_capacity);
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

    Uint32 width = 0;
    Uint32 height = 0;
    SDL_GPUTexture *const texture = miso__renderer_load_texture(path, &width, &height);
    if (!texture) {
        return MISO_ERR_IO;
    }

    for (uint32_t i = 1; i < MISO_TEXTURE_TABLE_MAX; i++) {
        if (!g_texture_table[i].texture) {
            g_texture_table[i] = (MisoTextureEntry){
                .texture = texture,
                .width = width,
                .height = height,
            };
            *out_texture = i;
            return MISO_OK;
        }
    }

    miso__renderer_destroy_texture(texture);
    return MISO_ERR_OUT_OF_MEMORY;
}

MisoResult miso_render_get_texture_info(const MisoEngine *const engine,
                                        const MisoTextureHandle texture,
                                        MisoTextureInfo *const out_info) {
    (void)engine;

    if (texture == 0 || texture >= MISO_TEXTURE_TABLE_MAX || !out_info || !g_texture_table[texture].texture) {
        return MISO_ERR_INVALID_ARG;
    }

    *out_info = (MisoTextureInfo){
        .width = g_texture_table[texture].width,
        .height = g_texture_table[texture].height,
    };
    return MISO_OK;
}

void miso_render_destroy_texture(const MisoEngine *const engine, const MisoTextureHandle texture) {
    (void)engine;

    if (texture == 0 || texture >= MISO_TEXTURE_TABLE_MAX || !g_texture_table[texture].texture) {
        return;
    }

    miso__renderer_destroy_texture(g_texture_table[texture].texture);
    g_texture_table[texture] = (MisoTextureEntry){0};
}

void miso_render_diag_submit_ui_texture_handle_debug(const MisoEngine *const engine,
                                                     const MisoTextureHandle texture,
                                                     const float x,
                                                     const float y,
                                                     const float width,
                                                     const float height) {
    (void)engine;

    if (texture == 0 || texture >= MISO_TEXTURE_TABLE_MAX || !g_texture_table[texture].texture) {
        return;
    }

    miso__renderer_draw_texture_debug(g_texture_table[texture].texture, x, y, width, height);
}

MisoResult miso_render_load_font(const MisoEngine *const engine,
                                 const char *const path,
                                 const float point_size,
                                 MisoFontHandle *const out_font) {
    (void)engine;

    if (!path || !out_font || point_size <= 0.0f) {
        return MISO_ERR_INVALID_ARG;
    }

    TTF_TextEngine *const text_engine = miso__renderer_get_text_engine();
    if (!text_engine) {
        return MISO_ERR_GPU;
    }

    TTF_Font *const font = TTF_OpenFont(path, point_size);
    if (!font) {
        return MISO_ERR_IO;
    }

    TTF_Text *const text = TTF_CreateText(text_engine, font, "", 0);
    if (!text) {
        TTF_CloseFont(font);
        return MISO_ERR_GPU;
    }

    for (MisoFontHandle i = 1; i < MISO_FONT_TABLE_MAX; i++) {
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

    miso__text_on_font_destroyed(font);

    if (g_font_table[font].text) {
        TTF_DestroyText(g_font_table[font].text);
    }
    TTF_CloseFont(g_font_table[font].font);

    g_font_table[font].text = nullptr;
    g_font_table[font].font = nullptr;
}

void miso_render_set_vsync(const MisoEngine *const engine, const bool enabled) {
    (void)engine;
    miso__renderer_set_vsync(enabled);
}

bool miso_render_get_vsync(const MisoEngine *const engine) {
    (void)engine;
    return miso__renderer_get_vsync();
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
                                const MisoSpriteInstance *restrict instances,
                                const int count) {
    (void)engine;

    if (texture == 0 || texture >= MISO_TEXTURE_TABLE_MAX || !instances || count <= 0 ||
        !g_texture_table[texture].texture) {
        return;
    }

    miso__renderer_draw_sprites(g_texture_table[texture].texture, instances, count);
}

void miso_render_submit_world_geometry(const MisoEngine *const engine,
                                       const MisoWorldVertex *const restrict vertices,
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

void miso_render_submit_world_lines(const MisoEngine *const engine,
                                    const float *const restrict vertices_xyz,
                                    const int vertex_count,
                                    const uint32_t rgba8) {
    (void)engine;

    if (!vertices_xyz || vertex_count <= 0 || (vertex_count & 1) != 0) {
        return;
    }

    miso__renderer_draw_line_batch(vertices_xyz, vertex_count, miso__color_from_rgba8(rgba8));
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
    miso__text_shutdown();

    for (uint32_t i = 1; i < MISO_TEXTURE_TABLE_MAX; i++) {
        if (g_texture_table[i].texture) {
            miso__renderer_destroy_texture(g_texture_table[i].texture);
            g_texture_table[i] = (MisoTextureEntry){0};
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

TTF_Font *miso__render_get_font_ptr(const MisoFontHandle font) {
    if (font == 0 || font >= MISO_FONT_TABLE_MAX || !g_font_table[font].font) {
        return nullptr;
    }

    return g_font_table[font].font;
}
