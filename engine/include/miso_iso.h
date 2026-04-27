#ifndef MISO_ISO_H
#define MISO_ISO_H

#include "miso_world.h"

typedef struct MisoIsoTileCoordF {
    float x;
    float y;
} MisoIsoTileCoordF;

/**
 * Converts integer tile coordinates to the world position of that isometric tile.
 *
 * The conversion uses MisoIsoMapDesc tile dimensions and map height to align
 * the top row around the map's isometric origin. Invalid descriptors or output
 * pointers are ignored.
 *
 * \param desc Isometric map description.
 * \param tile_x Tile x coordinate.
 * \param tile_y Tile y coordinate.
 * \param out_world_x Receives world x coordinate.
 * \param out_world_y Receives world y coordinate.
 */
void miso_iso_tile_to_world(const MisoIsoMapDesc *desc, int tile_x, int tile_y, float *out_world_x, float *out_world_y);

/**
 * Converts world coordinates to fractional isometric tile coordinates.
 *
 * Invalid descriptors return (0, 0). The result is not bounds checked against
 * the map.
 *
 * \param desc Isometric map description.
 * \param world_x World x coordinate.
 * \param world_y World y coordinate.
 * \return Fractional tile coordinate.
 */
MisoIsoTileCoordF miso_iso_world_to_tile_f(const MisoIsoMapDesc *desc, float world_x, float world_y);

/**
 * Converts world coordinates to floored integer tile coordinates.
 *
 * This is a coordinate conversion only; the result may be outside the map.
 * Invalid descriptors or output pointers are ignored.
 *
 * \param desc Isometric map description.
 * \param world_x World x coordinate.
 * \param world_y World y coordinate.
 * \param out_tile_x Receives floored tile x coordinate.
 * \param out_tile_y Receives floored tile y coordinate.
 */
void miso_iso_world_to_tile_floor(
    const MisoIsoMapDesc *desc, float world_x, float world_y, int *out_tile_x, int *out_tile_y);

#endif
