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

MisoPlacementFail miso_building_can_place(const MisoWorld *world, const MisoPlacementQuery *query);
MisoResult miso_building_place(MisoWorld *world,
                               MisoBuildingTypeId type_id,
                               int tx,
                               int ty,
                               int footprint_w,
                               int footprint_h,
                               MisoBuildingId *out_id);
MisoResult miso_building_remove(MisoWorld *world, MisoBuildingId building_id);

bool miso_building_pick_at_screen(
    const MisoWorld *world, const MisoEngine *engine, MisoCameraId camera_id, int sx, int sy, MisoBuildingId *out_id);
int miso_building_get_all(const MisoWorld *world, MisoBuildingInfo *out_items, int capacity);

#endif
