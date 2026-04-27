#ifndef MISO_WORLD_H
#define MISO_WORLD_H

#include "miso_camera.h"

typedef struct MisoWorld MisoWorld;

typedef uint32_t MisoLotId;

typedef struct MisoIsoMapDesc {
    int width_tiles;
    int height_tiles;
    int tile_w_px;
    int tile_h_px;
} MisoIsoMapDesc;

/**
 * Creates a tile occupancy world for an isometric map description.
 *
 * All dimensions in \p desc must be positive. The returned world owns its
 * occupancy and building storage and must be destroyed with miso_world_destroy().
 *
 * \return A world on success, or NULL for invalid arguments/allocation failure.
 * \param engine Engine associated with this world.
 * \param desc Isometric map description copied into the world.
 */
MisoWorld *miso_world_create(MisoEngine *engine, const MisoIsoMapDesc *desc);

/**
 * Destroys a world and all engine-owned world/building storage.
 *
 * Passing NULL is allowed.
 *
 * \param world World to destroy, or NULL.
 */
void miso_world_destroy(MisoWorld *world);

/**
 * Returns whether a tile is in bounds and unoccupied.
 *
 * Out-of-bounds tiles, invalid worlds, and occupied tiles all return false.
 *
 * \param world World to query.
 * \param tx Tile x coordinate.
 * \param ty Tile y coordinate.
 * \return true if the tile exists and is not occupied.
 */
bool miso_world_is_tile_free(const MisoWorld *world, int tx, int ty);

/**
 * Marks a tile occupied or free.
 *
 * \param world World to update.
 * \param tx Tile x coordinate.
 * \param ty Tile y coordinate.
 * \param occupied Occupancy value to store.
 * \return true if the tile was in bounds and updated; false otherwise.
 */
bool miso_world_set_tile_occupied(const MisoWorld *world, int tx, int ty, bool occupied);

/**
 * Converts a screen pixel coordinate through a camera into a world tile.
 *
 * \p out_tx and \p out_ty are written with the floored tile coordinate even
 * when the coordinate is outside the map. The return value indicates whether
 * that tile is inside the world bounds.
 *
 * \param world World whose map bounds are used.
 * \param engine Engine that owns the camera.
 * \param camera_id Camera used to convert screen to world coordinates.
 * \param sx Screen x coordinate in pixels.
 * \param sy Screen y coordinate in pixels.
 * \param out_tx Receives tile x coordinate.
 * \param out_ty Receives tile y coordinate.
 * \return true if the resulting tile is inside world bounds.
 */
bool miso_world_screen_to_tile(
    const MisoWorld *world, const MisoEngine *engine, MisoCameraId camera_id, int sx, int sy, int *out_tx, int *out_ty);

/**
 * Returns the immutable map description stored by the world.
 *
 * The pointer is owned by the world and remains valid until destruction.
 * Invalid worlds return NULL.
 *
 * \param world World to query.
 * \return Pointer to the world's map description, or NULL.
 */
const MisoIsoMapDesc *miso_world_get_desc(const MisoWorld *world);

#endif
