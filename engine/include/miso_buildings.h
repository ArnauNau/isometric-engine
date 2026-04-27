#ifndef MISO_BUILDINGS_H
#define MISO_BUILDINGS_H

#include "miso_world.h"

typedef uint32_t MisoBuildingId;
typedef uint32_t MisoBuildingTypeId;
typedef struct MisoBuildingInfo {
    MisoBuildingId id;
    MisoBuildingTypeId type_id;
    int tx;
    int ty;
    int footprint_w;
    int footprint_h;
} MisoBuildingInfo;

typedef struct MisoPlacementQuery {
    MisoBuildingTypeId type_id;
    int tx;
    int ty;
    int footprint_w;
    int footprint_h;
} MisoPlacementQuery;

typedef enum MisoPlacementFail {
    MISO_PLACE_OK = 0,
    MISO_PLACE_BLOCKED,
    MISO_PLACE_OUT_OF_BOUNDS,
    MISO_PLACE_RULE_VIOLATION
} MisoPlacementFail;

/**
 * Checks whether a rectangular building footprint can be placed.
 *
 * The current rule set only validates positive footprint dimensions, map
 * bounds, and tile occupancy. Invalid arguments or non-positive footprints are
 * reported as MISO_PLACE_RULE_VIOLATION.
 *
 * \param world World whose occupancy is checked.
 * \param query Placement query to validate.
 * \return Placement status.
 */
MisoPlacementFail miso_building_can_place(const MisoWorld *world, const MisoPlacementQuery *query);

/**
 * Places a building and marks all footprint tiles occupied.
 *
 * Placement fails if the footprint is invalid, out of bounds, or blocked.
 * Building ids are non-zero and monotonically assigned per world. \p out_id may
 * be NULL if the caller does not need the id.
 *
 * \param world World to update.
 * \param type_id Caller-defined building type id to store.
 * \param tx Top-left footprint tile x coordinate.
 * \param ty Top-left footprint tile y coordinate.
 * \param footprint_w Footprint width in tiles.
 * \param footprint_h Footprint height in tiles.
 * \param out_id Optional destination for the assigned building id.
 * \return MISO_OK, MISO_ERR_INVALID_ARG, or MISO_ERR_OUT_OF_MEMORY.
 * \sa miso_building_can_place()
 */
MisoResult miso_building_place(MisoWorld *world,
                               MisoBuildingTypeId type_id,
                               int tx,
                               int ty,
                               int footprint_w,
                               int footprint_h,
                               MisoBuildingId *out_id);

/**
 * Removes an active building and frees its footprint tiles.
 *
 * Removed building records are marked inactive but remain in internal storage;
 * ids are not reused. Passing id 0 is invalid.
 *
 * \param world World to update.
 * \param building_id Building id to remove.
 * \return MISO_OK, MISO_ERR_INVALID_ARG, or MISO_ERR_NOT_FOUND.
 */
MisoResult miso_building_remove(MisoWorld *world, MisoBuildingId building_id);

/**
 * Picks the active building whose footprint contains a screen position.
 *
 * The screen coordinate is converted through \p camera_id and the world tile
 * map. Returns false if the point is outside the map, no active building covers
 * the tile, or required pointers are invalid.
 *
 * \param world World to query.
 * \param engine Engine that owns the camera.
 * \param camera_id Camera used for screen-to-tile conversion.
 * \param sx Screen x coordinate in pixels.
 * \param sy Screen y coordinate in pixels.
 * \param out_id Receives the picked building id on success.
 * \return true if a building was picked.
 */
bool miso_building_pick_at_screen(
    const MisoWorld *world, const MisoEngine *engine, MisoCameraId camera_id, int sx, int sy, MisoBuildingId *out_id);

/**
 * Copies active building records into caller-provided storage.
 *
 * At most \p capacity items are written. Inactive/removed records are skipped.
 * Passing NULL storage or a non-positive capacity returns 0.
 *
 * \param world World to query.
 * \param out_items Destination array.
 * \param capacity Maximum number of entries to write.
 * \return Number of entries written, not the total active count when truncated.
 */
int miso_building_get_all(const MisoWorld *world, MisoBuildingInfo *out_items, int capacity);

#endif
