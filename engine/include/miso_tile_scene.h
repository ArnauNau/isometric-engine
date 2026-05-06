#ifndef MISO_TILE_SCENE_H
#define MISO_TILE_SCENE_H

#include "miso_camera.h"
#include "miso_engine.h"
#include "miso_render.h"
#include "miso_world.h"

#include <SDL3/SDL_stdinc.h> //SDL_MAX_UINT32

typedef struct MisoTileScene MisoTileScene;
typedef struct MisoTilemap MisoTilemap;
typedef struct MisoTileOverlay MisoTileOverlay;

typedef Uint32 MisoTileObjectId;
typedef Uint32 MisoTileObjectTypeId;
typedef Uint32 MisoTileVisualId;

/** Sentinel tile id used for cells without terrain. Empty tiles are not rendered. */
#define MISO_TILE_EMPTY SDL_MAX_UINT32

typedef Uint32 MisoTileFlags;
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

static inline MisoTileFlags miso_tile_user_flag(const MisoTileFlags index) {
    return index < MISO_TILE_FLAG_USER_COUNT ? (MisoTileFlags)(1u << (MISO_TILE_FLAG_USER_SHIFT + index)) : 0u;
}

typedef Uint8 MisoTileOccupancyMask;
typedef enum MisoTileOccupancyFlag : MisoTileOccupancyMask {
    MISO_TILE_OCCUPANCY_NONE = 0,
    MISO_TILE_OCCUPANCY_OBJECT = 1u << 0u,
    MISO_TILE_OCCUPANCY_AGENT = 1u << 1u,
} MisoTileOccupancyFlag;

typedef Uint8 MisoTilePlacementProblemMask;
typedef enum MisoTilePlacementProblem : MisoTilePlacementProblemMask {
    MISO_TILE_PLACE_OK = 0,
    MISO_TILE_PLACE_OUT_OF_BOUNDS = 1u << 0u,
    MISO_TILE_PLACE_OCCUPIED = 1u << 1u,
    MISO_TILE_PLACE_MISSING_REQUIRED_FLAGS = 1u << 2u,
    MISO_TILE_PLACE_HAS_FORBIDDEN_FLAGS = 1u << 3u,
    MISO_TILE_PLACE_MISSING_ADJACENCY = 1u << 4u,
    MISO_TILE_PLACE_INVALID_ARGUMENT = 1u << 5u,
    MISO_TILE_PLACE_MISSING_TERRAIN = 1u << 6u,
} MisoTilePlacementProblem;

typedef struct MisoTileSceneDesc {
    MisoIsoMapDesc map;
} MisoTileSceneDesc;

/**
 * Tilemap creation data.
 *
 * The texture must be a valid MisoTextureHandle. Atlas columns and rows may be
 * supplied explicitly, or set to 0 to derive the missing value from the loaded
 * texture dimensions and the scene tile pixel size. Derivation requires exact
 * divisibility; atlases with padding, spacing, margins, or uneven cells should
 * pass explicit atlas dimensions until a richer atlas resource format exists.
 */
typedef struct MisoTilemapDesc {
    /** Loaded texture handle containing the tile atlas. */
    MisoTextureHandle texture;
    /** Atlas column count, or 0 to derive from texture width and scene tile width. */
    Uint16 atlas_columns;
    /** Atlas row count, or 0 to derive from texture height and scene tile height. */
    Uint16 atlas_rows;
} MisoTilemapDesc;

typedef struct MisoTileOverlayDesc {
    /** Initial color for every overlay tile, encoded as 0xRRGGBBAA. */
    Uint32 clear_rgba8;
} MisoTileOverlayDesc;

typedef struct MisoTileFootprint {
    Uint16 width;
    Uint16 height;
    Uint16 anchor_x;
    Uint16 anchor_y;
} MisoTileFootprint;

typedef struct MisoTilePlacementQuery {
    int tile_x;
    int tile_y;
    MisoTileFlags required_tile_flags;
    MisoTileFlags forbidden_tile_flags;
    MisoTileFlags required_adjacent_tile_flags;
    MisoTileFootprint footprint;
    MisoTileOccupancyMask occupied_mask;
} MisoTilePlacementQuery;

typedef struct MisoTileObjectDesc {
    MisoTileObjectTypeId type_id;
    int tile_x;
    int tile_y;
    MisoTileFootprint footprint;
    MisoTileVisualId visual_id;
    uint8_t occupancy_mask;
    bool pickable;
    Uint64 game_ref;
} MisoTileObjectDesc;

typedef struct MisoTileObjectInfo {
    Uint64 game_ref;
    MisoTileObjectId id;
    MisoTileObjectTypeId type_id;
    int tile_x;
    int tile_y;
    MisoTileVisualId visual_id;
    MisoTileFootprint footprint;
    MisoTileOccupancyMask occupancy_mask;
    bool pickable;
} MisoTileObjectInfo;

typedef struct MisoTileSceneStats {
    Uint32 active_object_count;
    Uint32 object_storage_count;
    Uint32 object_capacity;
    Uint64 place_calls;
    Uint64 remove_calls;
    Uint64 remove_scan_steps;
} MisoTileSceneStats;

/**
 * Visual definition for tile objects rendered by the tile scene.
 *
 * The visual references a texture atlas tile by id. Atlas columns/rows follow
 * the same convention as MisoTilemapDesc: provide explicit values for custom
 * atlas layouts, or set either value to 0 for grid derivation from texture
 * dimensions and scene tile size.
 */
typedef struct MisoTileObjectVisualDesc {
    /** Visual id referenced by MisoTileObjectDesc::visual_id. */
    MisoTileVisualId visual_id;
    /** Loaded texture handle containing the visual atlas. */
    MisoTextureHandle texture;
    /** Atlas column count, or 0 to derive from texture width and scene tile width. */
    Uint16 atlas_columns;
    /** Atlas row count, or 0 to derive from texture height and scene tile height. */
    Uint16 atlas_rows;
    /** Atlas tile id used as the visual's top-left source tile. */
    Uint32 atlas_tile_id;
    /** Visual width in tile units. */
    int sprite_w_tiles;
    /** Visual height in tile units. */
    int sprite_h_tiles;
} MisoTileObjectVisualDesc;

MisoTileScene *miso_tile_scene_create(MisoEngine *engine, const MisoTileSceneDesc *desc);
void miso_tile_scene_destroy(MisoTileScene *scene);
const MisoIsoMapDesc *miso_tile_scene_get_desc(const MisoTileScene *scene);

/**
 * Creates a tilemap owned by the caller and associated with a tile scene.
 *
 * The tilemap stores tile ids, generic tile flags, render cache data, and an
 * optional tint overlay reference. It does not own the texture handle. If atlas
 * columns or rows are 0, they are derived from the texture metadata retained by
 * miso_render_load_texture().
 *
 * Returns NULL when arguments are invalid, texture metadata is unavailable, or
 * derived atlas dimensions do not divide evenly by the scene tile size.
 *
 * \param scene Tile scene that provides map dimensions and tile pixel size.
 * \param desc Tilemap creation data.
 * \return New tilemap, or NULL on failure.
 */
MisoTilemap *miso_tilemap_create(MisoTileScene *scene, const MisoTilemapDesc *desc);
void miso_tilemap_destroy(MisoTilemap *tilemap);

/**
 * Sets one tile id and marks the tilemap render cache dirty.
 *
 * \param tilemap Tilemap to mutate.
 * \param tx Tile x coordinate.
 * \param ty Tile y coordinate.
 * \param tile_id Atlas tile id.
 * \return true when the tile was updated.
 */
bool miso_tilemap_set_tile(MisoTilemap *tilemap, int tx, int ty, Uint32 tile_id);
Uint32 miso_tilemap_get_tile(const MisoTilemap *tilemap, int tx, int ty);
bool miso_tilemap_has_tile(const MisoTilemap *tilemap, int tx, int ty);
bool miso_tilemap_clear_tile(MisoTilemap *tilemap, int tx, int ty);
/**
 * Sets generic per-tile flags and marks the tilemap render cache dirty.
 *
 * \param tilemap Tilemap to mutate.
 * \param tx Tile x coordinate.
 * \param ty Tile y coordinate.
 * \param flags Bitmask of MisoTileFlag values and user flags.
 * \return true when the flags were updated.
 */
bool miso_tilemap_set_flags(MisoTilemap *tilemap, int tx, int ty, MisoTileFlags flags);
MisoTileFlags miso_tilemap_get_flags(const MisoTilemap *tilemap, int tx, int ty);
/**
 * Fills all tile ids and flags, then marks the tilemap render cache dirty.
 *
 * \param tilemap Tilemap to mutate.
 * \param tile_id Atlas tile id assigned to every tile.
 * \param flags Bitmask assigned to every tile.
 */
void miso_tilemap_fill(MisoTilemap *tilemap, Uint32 tile_id, MisoTileFlags flags);
void miso_tilemap_clear(MisoTilemap *tilemap);

/**
 * Creates an RGBA8 per-tile overlay buffer for a scene.
 *
 * Overlay data is CPU-owned by the engine and uploaded lazily when rendered as
 * a tilemap tint overlay. Colors use 0xRRGGBBAA. The overlay does not encode
 * game semantics; games decide what each color means.
 *
 * \param scene Scene whose tile dimensions define the overlay buffer size.
 * \param desc Overlay creation data.
 * \return New overlay, or NULL on failure.
 */
MisoTileOverlay *miso_tile_overlay_create(MisoTileScene *scene, const MisoTileOverlayDesc *desc);
void miso_tile_overlay_destroy(MisoTileOverlay *overlay);
void miso_tile_overlay_clear(MisoTileOverlay *overlay, uint32_t rgba8);
bool miso_tile_overlay_set_tile_rgba8(MisoTileOverlay *overlay, int tx, int ty, Uint32 rgba8);
Uint32 miso_tile_overlay_get_tile_rgba8(const MisoTileOverlay *overlay, int tx, int ty);
void miso_tile_overlay_fill_footprint(
    MisoTileOverlay *overlay, int tile_x, int tile_y, MisoTileFootprint footprint, Uint32 rgba8);
/**
 * Attaches or detaches a terrain tint overlay for tilemap rendering.
 *
 * Passing NULL disables tinting. Strength is clamped by the renderer. The
 * current implementation tints terrain sprites; object/agent tinting remains a
 * separate policy decision.
 *
 * \param tilemap Tilemap whose terrain render should use the overlay.
 * \param overlay Overlay to sample, or NULL to disable tinting.
 * \param strength Tint strength in the range [0, 1].
 */
void miso_tilemap_set_tint_overlay(MisoTilemap *tilemap, MisoTileOverlay *overlay, float strength);

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
bool miso_tile_scene_get_stats(const MisoTileScene *scene, MisoTileSceneStats *out_stats);
void miso_tile_scene_reset_stats(MisoTileScene *scene);

/**
 * Registers or replaces a visual definition used by tile objects.
 *
 * This function validates or derives atlas grid dimensions using texture
 * metadata, mirroring miso_tilemap_create(). The texture handle is referenced
 * but not owned by the scene.
 *
 * \param scene Scene that owns the visual registry.
 * \param desc Visual definition to register or replace.
 * \return MISO_OK on success or an error code.
 */
MisoResult miso_tile_scene_set_object_visual(MisoTileScene *scene, const MisoTileObjectVisualDesc *desc);

/**
 * Renders active tile objects through their registered visuals.
 *
 * Object placement, occupancy, and visual registration are engine-owned; game
 * behavior and semantic ids remain game-owned through type_id/game_ref.
 *
 * \param engine Engine whose renderer is active.
 * \param scene Tile scene containing objects and visuals.
 * \param camera_id Camera used for world rendering.
 */
void miso_tile_scene_render_objects(const MisoEngine *engine, MisoTileScene *scene, MisoCameraId camera_id);

/**
 * Converts fractional tile coordinates to world coordinates.
 *
 * The output pointers must not alias each other.
 */
bool miso_tile_scene_tile_to_world(
    const MisoTileScene *scene, float tile_x, float tile_y, float *restrict out_world_x, float *restrict out_world_y);

/**
 * Converts world coordinates to fractional tile coordinates.
 *
 * The output pointers must not alias each other.
 */
bool miso_tile_scene_world_to_tile(
    const MisoTileScene *scene, float world_x, float world_y, float *restrict out_tile_x, float *restrict out_tile_y);
float miso_tile_scene_depth_at_tile(const MisoTileScene *scene, float tile_x, float tile_y);

/**
 * Renders tilemap terrain for the scene using the active atlas and render cache.
 *
 * The render cache is rebuilt only when tile ids or tile flags change, then
 * submitted as sprite instances. If a tint overlay is attached, dirty overlay
 * data is uploaded before drawing and sampled by the terrain shader.
 *
 * \param engine Engine whose renderer is active.
 * \param tilemap Tilemap terrain to render.
 * \param scene Scene that provides map coordinates and tile dimensions.
 * \param camera_id Camera used for world rendering.
 */
void miso_tilemap_render(const MisoEngine *engine,
                         MisoTilemap *tilemap,
                         const MisoTileScene *scene,
                         MisoCameraId camera_id);

#endif
