#ifndef MISO_ISO_H
#define MISO_ISO_H

#include "miso_world.h"

typedef struct MisoIsoTileCoordF {
    float x;
    float y;
} MisoIsoTileCoordF;

void miso_iso_tile_to_world(const MisoIsoMapDesc *desc, int tile_x, int tile_y, float *out_world_x, float *out_world_y);
MisoIsoTileCoordF miso_iso_world_to_tile_f(const MisoIsoMapDesc *desc, float world_x, float world_y);
void miso_iso_world_to_tile_floor(
    const MisoIsoMapDesc *desc, float world_x, float world_y, int *out_tile_x, int *out_tile_y);

#endif
