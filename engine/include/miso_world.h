#ifndef MISO_WORLD_H
#define MISO_WORLD_H

#include "miso_camera.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct MisoWorld MisoWorld;

typedef uint32_t MisoLotId;

typedef struct MisoIsoMapDesc {
    int width_tiles;
    int height_tiles;
    int tile_w_px;
    int tile_h_px;
} MisoIsoMapDesc;

MisoWorld *miso_world_create(MisoEngine *engine, const MisoIsoMapDesc *desc);
void miso_world_destroy(MisoWorld *world);

bool miso_world_is_tile_free(const MisoWorld *world, int tx, int ty);
bool miso_world_set_tile_occupied(const MisoWorld *world, int tx, int ty, bool occupied);

bool miso_world_screen_to_tile(
    const MisoWorld *world, const MisoEngine *engine, MisoCameraId camera_id, int sx, int sy, int *out_tx, int *out_ty);
const MisoIsoMapDesc *miso_world_get_desc(const MisoWorld *world);

#endif
