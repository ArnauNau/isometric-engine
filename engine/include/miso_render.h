#ifndef MISO_RENDER_H
#define MISO_RENDER_H

#include "miso_camera.h"
#include "miso_engine.h"

typedef uint32_t MisoTextureHandle;
typedef uint32_t MisoFontHandle;

typedef struct MisoSpriteInstance {
    float x;
    float y;
    float z;
    float flags;
    float w;
    float h;
    float tile_x;
    float tile_y;
    float u;
    float v;
    float uw;
    float vh;
} MisoSpriteInstance;

typedef struct MisoWorldVertex {
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
} MisoWorldVertex;

MisoResult miso_render_load_texture(const MisoEngine *engine, const char *path, MisoTextureHandle *out_texture);
void miso_render_destroy_texture(const MisoEngine *engine, MisoTextureHandle texture);
MisoResult
miso_render_load_font(const MisoEngine *engine, const char *path, float point_size, MisoFontHandle *out_font);
void miso_render_destroy_font(const MisoEngine *engine, MisoFontHandle font);
void miso_render_set_vsync(const MisoEngine *engine, bool enabled);
bool miso_render_get_vsync(const MisoEngine *engine);

void miso_render_begin_world(const MisoEngine *engine, MisoCameraId camera_id);
void miso_render_set_water_params(const MisoEngine *engine, float time, float speed, float amplitude, float phase);
void miso_render_submit_sprites(const MisoEngine *engine,
                                MisoTextureHandle texture,
                                const MisoSpriteInstance *instances,
                                int count);
void miso_render_submit_world_geometry(const MisoEngine *engine, const MisoWorldVertex *vertices, int count);
void miso_render_submit_world_lines(const MisoEngine *engine,
                                    const float *vertices_xyz,
                                    int vertex_count,
                                    uint32_t rgba8);
void miso_render_end_world(const MisoEngine *engine);

void miso_render_begin_ui(const MisoEngine *engine);
void miso_render_submit_ui_rect(const MisoEngine *engine, float x, float y, float w, float h, uint32_t rgba8);
void miso_render_submit_ui_text(
    const MisoEngine *engine, MisoFontHandle font, const char *text, float x, float y, uint32_t rgba8);
void miso_render_end_ui(const MisoEngine *engine);

#endif
