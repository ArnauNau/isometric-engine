#include "miso_iso.h"

#include <SDL3/SDL.h>

void miso_iso_tile_to_world(const MisoIsoMapDesc *const desc,
                            const int tile_x,
                            const int tile_y,
                            float *const restrict out_world_x,
                            float *const restrict out_world_y) {
    if (!desc || !out_world_x || !out_world_y) {
        return;
    }

    const float iso_w = (float)desc->tile_w_px;
    const float iso_h = (float)desc->tile_h_px * 0.5f;
    const float start_x = ((float)(desc->height_tiles - 1) * iso_w) * 0.5f;

    *out_world_x = start_x + (float)(tile_x - tile_y) * (iso_w * 0.5f);
    *out_world_y = (float)(tile_x + tile_y) * (iso_h * 0.5f);
}

MisoIsoTileCoordF miso_iso_world_to_tile_f(const MisoIsoMapDesc *const desc, const float world_x, const float world_y) {
    if (!desc) {
        return (MisoIsoTileCoordF){0.0f, 0.0f};
    }

    const float iso_w = (float)desc->tile_w_px;
    const float iso_h = (float)desc->tile_h_px * 0.5f;
    const float start_x = ((float)(desc->height_tiles - 1) * iso_w) * 0.5f;
    const float origin_x = start_x + iso_w * 0.5f;
    const float half_iso_w = iso_w * 0.5f;
    const float half_iso_h = iso_h * 0.5f;

    const float term_a = (world_x - origin_x) / half_iso_w;
    const float term_b = world_y / half_iso_h;

    return (MisoIsoTileCoordF){
        .x = (term_a + term_b) * 0.5f,
        .y = (term_b - term_a) * 0.5f,
    };
}

void miso_iso_world_to_tile_floor(const MisoIsoMapDesc *const desc,
                                  const float world_x,
                                  const float world_y,
                                  int *const restrict out_tile_x,
                                  int *const restrict out_tile_y) {
    if (!desc || !out_tile_x || !out_tile_y) {
        return;
    }

    const MisoIsoTileCoordF tile = miso_iso_world_to_tile_f(desc, world_x, world_y);
    *out_tile_x = (int)SDL_floorf(tile.x);
    *out_tile_y = (int)SDL_floorf(tile.y);
}
