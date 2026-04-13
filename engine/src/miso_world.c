#include "miso_world.h"

#include "internal/miso__world_internal.h"
#include "miso_iso.h"

#include <SDL3/SDL.h>

static bool miso__in_bounds(const MisoWorld *const world, const int tx, const int ty) {
    return world && tx >= 0 && ty >= 0 && tx < world->map.width_tiles && ty < world->map.height_tiles;
}

static int miso__tile_index(const MisoWorld *const world, const int tx, const int ty) {
    return ty * world->map.width_tiles + tx;
}

MisoWorld *miso_world_create(MisoEngine *const engine, const MisoIsoMapDesc *const desc) {
    if (!engine || !desc || desc->width_tiles <= 0 || desc->height_tiles <= 0 || desc->tile_w_px <= 0 ||
        desc->tile_h_px <= 0) {
        return nullptr;
    }

    MisoWorld *world = SDL_calloc(1, sizeof(MisoWorld));
    if (!world) {
        return nullptr;
    }

    world->engine = engine;
    world->map = *desc;

    const size_t tile_count = (size_t)desc->width_tiles * (size_t)desc->height_tiles;
    world->occupied = SDL_calloc(tile_count, sizeof(bool));
    if (!world->occupied) {
        SDL_free(world);
        return nullptr;
    }

    world->next_building_id = 1;
    return world;
}

void miso_world_destroy(MisoWorld *const world) {
    if (!world) {
        return;
    }

    SDL_free(world->occupied);
    SDL_free(world->buildings);
    SDL_free(world);
}

bool miso_world_is_tile_free(const MisoWorld *const world, const int tx, const int ty) {
    if (!miso__in_bounds(world, tx, ty)) {
        return false;
    }

    return !world->occupied[miso__tile_index(world, tx, ty)];
}

bool miso_world_set_tile_occupied(const MisoWorld *const world, const int tx, const int ty, const bool occupied) {
    if (!miso__in_bounds(world, tx, ty)) {
        return false;
    }

    world->occupied[miso__tile_index(world, tx, ty)] = occupied;
    return true;
}

bool miso_world_screen_to_tile(const MisoWorld *const world,
                               const MisoEngine *const engine,
                               const MisoCameraId camera_id,
                               const int sx,
                               const int sy,
                               int *const out_tx,
                               int *const out_ty) {
    if (!world || !engine || !out_tx || !out_ty) {
        return false;
    }

    const MisoVec2 world_pos = miso_camera_screen_to_world(engine, camera_id, sx, sy);

    int tx = 0;
    int ty = 0;
    miso_iso_world_to_tile_floor(&world->map, world_pos.x, world_pos.y, &tx, &ty);

    *out_tx = tx;
    *out_ty = ty;

    return miso__in_bounds(world, tx, ty);
}

const MisoIsoMapDesc *miso_world_get_desc(const MisoWorld *const world) {
    if (!world) {
        return nullptr;
    }
    return &world->map;
}
