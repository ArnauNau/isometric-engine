#ifndef MISO_TILE_SCENE_H
#define MISO_TILE_SCENE_H

#include "miso_camera.h"
#include "miso_engine.h"
#include "miso_render.h"
#include "miso_world.h"

typedef struct MisoTileScene MisoTileScene;
typedef struct MisoTilemap MisoTilemap;

typedef uint32_t MisoTileObjectId;
typedef uint32_t MisoTileObjectTypeId;
typedef uint32_t MisoTileVisualId;

typedef uint32_t MisoTileFlags;
typedef enum MisoTileFlag : MisoTileFlags {
    MISO_TILE_FLAG_NONE = 0,
    MISO_TILE_FLAG_WALKABLE = 1u << 0u,
    MISO_TILE_FLAG_BUILDABLE = 1u << 1u,
    MISO_TILE_FLAG_PATH = 1u << 2u,
    MISO_TILE_FLAG_BLOCKS_OBJECTS = 1u << 3u,
    MISO_TILE_FLAG_BLOCKS_AGENTS = 1u << 4u,
    MISO_TILE_FLAG_WATER = 1u << 5u,

    MISO_TILE_FLAG_USER_COUNT = 16u,
    MISO_TILE_FLAG_USER_SHIFT = 16u,
    MISO_TILE_FLAG_USER_FIRST = 1u << MISO_TILE_FLAG_USER_SHIFT,
    MISO_TILE_FLAG_USER_MASK = 0xFFFF0000u
} MisoTileFlag;


static inline MisoTileFlags miso_tile_user_flag(const uint32_t index) {
    return index < MISO_TILE_FLAG_USER_COUNT
        ? (MisoTileFlags)(1u << (MISO_TILE_FLAG_USER_SHIFT + index))
        : 0u;
}

typedef uint8_t MisoTileOccupancyMask;
typedef enum MisoTileOccupancyFlag : MisoTileOccupancyMask {
    MISO_TILE_OCCUPANCY_NONE = 0,
    MISO_TILE_OCCUPANCY_OBJECT = 1u << 0u,
    MISO_TILE_OCCUPANCY_AGENT = 1u << 1u,
} MisoTileOccupancyFlag;

typedef uint8_t MisoTilePlacementProblemMask;
typedef enum MisoTilePlacementProblem : MisoTilePlacementProblemMask {
    MISO_TILE_PLACE_OK = 0,
    MISO_TILE_PLACE_OUT_OF_BOUNDS = 1u << 0u,
    MISO_TILE_PLACE_OCCUPIED = 1u << 1u,
    MISO_TILE_PLACE_MISSING_REQUIRED_FLAGS = 1u << 2u,
    MISO_TILE_PLACE_HAS_FORBIDDEN_FLAGS = 1u << 3u,
    MISO_TILE_PLACE_MISSING_ADJACENCY = 1u << 4u,
    MISO_TILE_PLACE_INVALID_ARGUMENT = 1u << 5u,
} MisoTilePlacementProblem;

typedef struct MisoTileSceneDesc {
    MisoIsoMapDesc map;
} MisoTileSceneDesc;

typedef struct MisoTilemapDesc {
    MisoTextureHandle texture;
    uint16_t atlas_columns;
    uint16_t atlas_rows;
} MisoTilemapDesc;

typedef struct MisoTileFootprint {
    int width;
    int height;
    int anchor_x;
    int anchor_y;
} MisoTileFootprint;

typedef struct MisoTilePlacementQuery {
    int tile_x;
    int tile_y;
    MisoTileFootprint footprint;
    uint32_t required_tile_flags;
    uint32_t forbidden_tile_flags;
    uint8_t occupied_mask;
    uint32_t required_adjacent_tile_flags;
} MisoTilePlacementQuery;

typedef struct MisoTileObjectDesc {
    MisoTileObjectTypeId type_id;
    int tile_x;
    int tile_y;
    MisoTileFootprint footprint;
    MisoTileVisualId visual_id;
    uint8_t occupancy_mask;
    bool pickable;
    uint64_t game_ref;
} MisoTileObjectDesc;

typedef struct MisoTileObjectInfo {
    MisoTileObjectId id;
    MisoTileObjectTypeId type_id;
    int tile_x;
    int tile_y;
    MisoTileFootprint footprint;
    MisoTileVisualId visual_id;
    uint8_t occupancy_mask;
    bool pickable;
    uint64_t game_ref;
} MisoTileObjectInfo;

typedef struct MisoTileObjectVisualDesc {
    MisoTileVisualId visual_id;
    MisoTextureHandle texture;
    uint16_t atlas_columns;
    uint16_t atlas_rows;
    uint32_t atlas_tile_id;
    int sprite_w_tiles;
    int sprite_h_tiles;
} MisoTileObjectVisualDesc;

MisoTileScene *miso_tile_scene_create(MisoEngine *engine, const MisoTileSceneDesc *desc);
void miso_tile_scene_destroy(MisoTileScene *scene);
const MisoIsoMapDesc *miso_tile_scene_get_desc(const MisoTileScene *scene);

MisoTilemap *miso_tilemap_create(MisoTileScene *scene, const MisoTilemapDesc *desc);
void miso_tilemap_destroy(MisoTilemap *tilemap);

bool miso_tilemap_set_tile(MisoTilemap *tilemap, int tx, int ty, uint32_t tile_id);
uint32_t miso_tilemap_get_tile(const MisoTilemap *tilemap, int tx, int ty);
bool miso_tilemap_set_flags(MisoTilemap *tilemap, int tx, int ty, uint32_t flags);
uint32_t miso_tilemap_get_flags(const MisoTilemap *tilemap, int tx, int ty);
void miso_tilemap_fill(MisoTilemap *tilemap, uint32_t tile_id, uint32_t flags);

MisoTilePlacementProblem miso_tile_scene_check_placement(const MisoTileScene *scene,
                                                         const MisoTilemap *tilemap,
                                                         const MisoTilePlacementQuery *query);

MisoResult miso_tile_scene_place_object(MisoTileScene *scene,
                                        const MisoTilemap *tilemap,
                                        const MisoTileObjectDesc *desc,
                                        MisoTileObjectId *out_id);
MisoResult miso_tile_scene_remove_object(MisoTileScene *scene, MisoTileObjectId object_id);
void miso_tile_scene_clear_objects(MisoTileScene *scene);
bool miso_tile_scene_pick_object_at_tile(const MisoTileScene *scene, int tx, int ty, MisoTileObjectId *out_id);
bool miso_tile_scene_pick_object_at_screen(const MisoTileScene *scene,
                                           const MisoEngine *engine,
                                           MisoCameraId camera_id,
                                           int sx,
                                           int sy,
                                           MisoTileObjectId *out_id);
int miso_tile_scene_get_objects(const MisoTileScene *scene, MisoTileObjectInfo *out_items, int capacity);

MisoResult miso_tile_scene_set_object_visual(MisoTileScene *scene, const MisoTileObjectVisualDesc *desc);
void miso_tile_scene_render_objects(const MisoEngine *engine, MisoTileScene *scene, MisoCameraId camera_id);

bool miso_tile_scene_tile_to_world(
    const MisoTileScene *scene, float tile_x, float tile_y, float *out_world_x, float *out_world_y);
bool miso_tile_scene_world_to_tile(
    const MisoTileScene *scene, float world_x, float world_y, float *out_tile_x, float *out_tile_y);
float miso_tile_scene_depth_at_tile(const MisoTileScene *scene, float tile_x, float tile_y);

void miso_tilemap_render(const MisoEngine *engine,
                         MisoTilemap *tilemap,
                         const MisoTileScene *scene,
                         MisoCameraId camera_id);

#endif
