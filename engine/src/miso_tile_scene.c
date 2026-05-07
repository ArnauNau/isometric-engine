#include "miso_tile_scene.h"

#include "internal/miso__renderer_backend.h"
#include "miso_iso.h"

#include <SDL3/SDL.h>

#define MISO_TILE_OBJECT_INDEX_INVALID SDL_MAX_UINT32

typedef struct MisoTileObjectRecord {
    Uint64 game_ref;
    MisoTileFootprint footprint;
    MisoTileObjectId id;
    MisoTileObjectTypeId type_id;
    MisoTileVisualId visual_id;
    Sint32 tile_x;
    Sint32 tile_y;
    MisoTileOccupancyMask occupancy_mask;
    bool pickable;
    bool active;
} MisoTileObjectRecord;

typedef struct MisoTileObjectVisualRecord {
    MisoTileVisualId visual_id;
    MisoTextureHandle texture;
    Uint16 atlas_columns;
    Uint16 atlas_rows;
    Uint32 atlas_tile_id;
    int sprite_w_tiles;
    int sprite_h_tiles;
} MisoTileObjectVisualRecord;

struct MisoTileScene {
    MisoIsoMapDesc map;
    Uint32 object_count;
    Uint32 active_object_count;
    Uint32 object_capacity;
    Uint32 visual_count;
    Uint32 visual_capacity;
    Uint32 object_render_capacity;
    MisoTileObjectId next_object_id;
    Uint32 object_index_capacity;
    MisoEngine *engine;
    MisoTileOccupancyMask *occupancy;
    MisoTileObjectRecord *objects;
    Uint32 *object_indices_by_id;
    MisoTileObjectVisualRecord *visuals;
    MisoSpriteInstance *object_render_cache;
    MisoTileSceneStats stats;
};

struct MisoTilemap {
    MisoTextureHandle texture;
    Uint16 atlas_columns;
    Uint16 atlas_rows;
    Uint32 render_cache_count;
    float tint_overlay_strength;
    bool cache_dirty;
    MisoTileScene *scene;
    Uint32 *tiles;
    Uint32 *flags;
    MisoSpriteInstance *render_cache;
    MisoTileOverlay *tint_overlay;
};

struct MisoTileOverlay {
    MisoTileScene *scene;
    Uint8 *rgba8;
    SDL_GPUTexture *texture;
    bool dirty;
};

static bool miso__tile_scene_valid_desc(const MisoTileSceneDesc *const desc) {
    return desc && desc->map.width_tiles > 0 && desc->map.height_tiles > 0 && desc->map.tile_w_px > 0 &&
           desc->map.tile_h_px > 0;
}

static bool miso__tile_scene_resolve_atlas_grid(const MisoTileScene *const scene,
                                                const MisoTextureHandle texture,
                                                const Uint16 requested_columns,
                                                const Uint16 requested_rows,
                                                Uint16 *const out_columns,
                                                Uint16 *const out_rows) {
    if (!scene || texture == 0 || !out_columns || !out_rows) {
        return false;
    }

    if (requested_columns > 0 && requested_rows > 0) {
        *out_columns = requested_columns;
        *out_rows = requested_rows;
        return true;
    }

    if (scene->map.tile_w_px <= 0 || scene->map.tile_h_px <= 0) {
        return false;
    }

    MisoTextureInfo texture_info = {0};
    if (miso_render_get_texture_info(scene->engine, texture, &texture_info) != MISO_OK || texture_info.width == 0 ||
        texture_info.height == 0) {
        return false;
    }

    Uint32 columns = requested_columns;
    Uint32 rows = requested_rows;
    if (columns == 0) {
        const Uint32 tile_w = (Uint32)scene->map.tile_w_px;
        if (texture_info.width % tile_w != 0U) {
            return false;
        }
        columns = texture_info.width / tile_w;
    }
    if (rows == 0) {
        const Uint32 tile_h = (Uint32)scene->map.tile_h_px;
        if (texture_info.height % tile_h != 0U) {
            return false;
        }
        rows = texture_info.height / tile_h;
    }

    if (columns == 0 || rows == 0 || columns > SDL_MAX_UINT16 || rows > SDL_MAX_UINT16) {
        return false;
    }

    *out_columns = (Uint16)columns;
    *out_rows = (Uint16)rows;
    return true;
}

static size_t miso__tile_scene_tile_count(const MisoTileScene *const scene) {
    return (size_t)scene->map.width_tiles * (size_t)scene->map.height_tiles;
}

static bool miso__tile_scene_in_bounds(const MisoTileScene *const scene, const int tx, const int ty) {
    return scene && tx >= 0 && ty >= 0 && tx < scene->map.width_tiles && ty < scene->map.height_tiles;
}

static inline size_t miso__tile_scene_index(const MisoTileScene *const scene, const int tx, const int ty) {
    return (size_t)ty * (size_t)scene->map.width_tiles + (size_t)tx;
}

static bool miso__tilemap_valid_tile_id(const MisoTilemap *const tilemap, const Uint32 tile_id) {
    if (!tilemap) {
        return false;
    }
    if (tile_id == MISO_TILE_EMPTY) {
        return true;
    }

    return tile_id < (Uint32)tilemap->atlas_columns * (Uint32)tilemap->atlas_rows;
}

static bool miso__tile_footprint_valid(const MisoTileFootprint *const footprint) {
    return footprint && footprint->width > 0 && footprint->height > 0 && footprint->anchor_x >= 0 &&
           footprint->anchor_y >= 0 && footprint->anchor_x < footprint->width &&
           footprint->anchor_y < footprint->height;
}

static void miso__tile_overlay_store_rgba8(Uint8 *const dst, const Uint32 rgba8) {
    dst[0] = (Uint8)((rgba8 >> 24U) & 0xFFU);
    dst[1] = (Uint8)((rgba8 >> 16U) & 0xFFU);
    dst[2] = (Uint8)((rgba8 >> 8U) & 0xFFU);
    dst[3] = (Uint8)(rgba8 & 0xFFU);
}

static Uint32 miso__tile_overlay_load_rgba8(const Uint8 *const src) {
    return ((Uint32)src[0] << 24U) | ((Uint32)src[1] << 16U) | ((Uint32)src[2] << 8U) | (Uint32)src[3];
}

static void miso__tile_footprint_bounds(const int anchor_tx,
                                        const int anchor_ty,
                                        const MisoTileFootprint *const footprint,
                                        int *const out_min_x,
                                        int *const out_min_y,
                                        int *const out_max_x,
                                        int *const out_max_y) {
    const int min_x = anchor_tx - footprint->anchor_x;
    const int min_y = anchor_ty - footprint->anchor_y;
    *out_min_x = min_x;
    *out_min_y = min_y;
    *out_max_x = min_x + footprint->width - 1;
    *out_max_y = min_y + footprint->height - 1;
}

static bool miso__tile_scene_ensure_object_capacity(MisoTileScene *const scene) {
    if (scene->object_count < scene->object_capacity) {
        return true;
    }

    const Uint32 new_capacity = scene->object_capacity == 0 ? 64U : scene->object_capacity * 2U;
    MisoTileObjectRecord *const new_objects = SDL_realloc(scene->objects, sizeof(MisoTileObjectRecord) * new_capacity);
    if (!new_objects) {
        return false;
    }

    SDL_memset(new_objects + scene->object_capacity,
               0,
               sizeof(MisoTileObjectRecord) * (new_capacity - scene->object_capacity));
    scene->objects = new_objects;
    scene->object_capacity = new_capacity;
    return true;
}

static bool miso__tile_scene_ensure_object_index_capacity(MisoTileScene *const scene,
                                                          const MisoTileObjectId object_id) {
    if (!scene || object_id == 0) {
        return false;
    }
    if (object_id < scene->object_index_capacity) {
        return true;
    }

    Uint32 new_capacity = scene->object_index_capacity == 0 ? 64U : scene->object_index_capacity;
    while (new_capacity <= object_id) {
        new_capacity *= 2U;
    }

    Uint32 *const new_indices = SDL_realloc(scene->object_indices_by_id, sizeof(Uint32) * new_capacity);
    if (!new_indices) {
        return false;
    }

    SDL_memset4(new_indices + scene->object_index_capacity,
                MISO_TILE_OBJECT_INDEX_INVALID,
                new_capacity - scene->object_index_capacity);
    scene->object_indices_by_id = new_indices;
    scene->object_index_capacity = new_capacity;
    return true;
}

static MisoTileObjectRecord *
miso__tile_scene_find_object(MisoTileScene *const scene, const MisoTileObjectId object_id, Uint32 *const out_index) {
    if (!scene || object_id == 0 || object_id >= scene->object_index_capacity || !scene->object_indices_by_id) {
        return nullptr;
    }

    const Uint32 index = scene->object_indices_by_id[object_id];
    if (index == MISO_TILE_OBJECT_INDEX_INVALID || index >= scene->object_count) {
        return nullptr;
    }

    MisoTileObjectRecord *const record = &scene->objects[index];
    if (!record->active || record->id != object_id) {
        return nullptr;
    }
    if (out_index) {
        *out_index = index;
    }
    return record;
}

static bool miso__tile_scene_ensure_visual_capacity(MisoTileScene *const scene) {
    if (scene->visual_count < scene->visual_capacity) {
        return true;
    }

    const Uint32 new_capacity = scene->visual_capacity == 0 ? 16U : scene->visual_capacity * 2U;
    MisoTileObjectVisualRecord *const new_visuals =
        SDL_realloc(scene->visuals, sizeof(MisoTileObjectVisualRecord) * new_capacity);
    if (!new_visuals) {
        return false;
    }

    SDL_memset(new_visuals + scene->visual_capacity,
               0,
               sizeof(MisoTileObjectVisualRecord) * (new_capacity - scene->visual_capacity));
    scene->visuals = new_visuals;
    scene->visual_capacity = new_capacity;
    return true;
}

static bool miso__tile_scene_ensure_object_render_capacity(MisoTileScene *const scene, const Uint32 capacity) {
    if (capacity <= scene->object_render_capacity) {
        return true;
    }

    Uint32 new_capacity = scene->object_render_capacity == 0 ? 64U : scene->object_render_capacity;
    while (new_capacity < capacity) {
        new_capacity *= 2U;
    }

    MisoSpriteInstance *const new_cache =
        SDL_realloc(scene->object_render_cache, sizeof(MisoSpriteInstance) * new_capacity);
    if (!new_cache) {
        return false;
    }

    scene->object_render_cache = new_cache;
    scene->object_render_capacity = new_capacity;
    return true;
}

static MisoTileObjectVisualRecord *miso__tile_scene_find_visual(MisoTileScene *const scene,
                                                                const MisoTileVisualId visual_id) {
    if (!scene || visual_id == 0) {
        return nullptr;
    }

    for (Uint32 i = 0; i < scene->visual_count; i++) {
        if (scene->visuals[i].visual_id == visual_id) {
            return &scene->visuals[i];
        }
    }

    return nullptr;
}

static const MisoTileObjectVisualRecord *miso__tile_scene_find_visual_const(const MisoTileScene *const scene,
                                                                            const MisoTileVisualId visual_id) {
    if (!scene || visual_id == 0) {
        return nullptr;
    }

    for (Uint32 i = 0; i < scene->visual_count; i++) {
        if (scene->visuals[i].visual_id == visual_id) {
            return &scene->visuals[i];
        }
    }

    return nullptr;
}

static bool miso__tile_scene_has_adjacent_flags(const MisoTileScene *const scene,
                                                const MisoTilemap *const tilemap,
                                                const int min_x,
                                                const int min_y,
                                                const int max_x,
                                                const int max_y,
                                                const Uint32 required_flags) {
    if (required_flags == 0) {
        return true;
    }

    for (int x = min_x; x <= max_x; x++) {
        const int top_y = min_y - 1;
        const int bottom_y = max_y + 1;
        if (miso__tile_scene_in_bounds(scene, x, top_y)) {
            const size_t idx = miso__tile_scene_index(scene, x, top_y);
            if (tilemap->tiles[idx] != MISO_TILE_EMPTY && (tilemap->flags[idx] & required_flags) == required_flags) {
                return true;
            }
        }
        if (miso__tile_scene_in_bounds(scene, x, bottom_y)) {
            const size_t idx = miso__tile_scene_index(scene, x, bottom_y);
            if (tilemap->tiles[idx] != MISO_TILE_EMPTY && (tilemap->flags[idx] & required_flags) == required_flags) {
                return true;
            }
        }
    }

    for (int y = min_y; y <= max_y; y++) {
        const int left_x = min_x - 1;
        const int right_x = max_x + 1;
        if (miso__tile_scene_in_bounds(scene, left_x, y)) {
            const size_t idx = miso__tile_scene_index(scene, left_x, y);
            if (tilemap->tiles[idx] != MISO_TILE_EMPTY && (tilemap->flags[idx] & required_flags) == required_flags) {
                return true;
            }
        }
        if (miso__tile_scene_in_bounds(scene, right_x, y)) {
            const size_t idx = miso__tile_scene_index(scene, right_x, y);
            if (tilemap->tiles[idx] != MISO_TILE_EMPTY && (tilemap->flags[idx] & required_flags) == required_flags) {
                return true;
            }
        }
    }

    return false;
}

static bool miso__tilemap_footprint_has_terrain(const MisoTileScene *const scene,
                                                const MisoTilemap *const tilemap,
                                                const int min_x,
                                                const int min_y,
                                                const int max_x,
                                                const int max_y) {
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            if (!miso__tile_scene_in_bounds(scene, x, y)) {
                return false;
            }
            if (tilemap->tiles[miso__tile_scene_index(scene, x, y)] == MISO_TILE_EMPTY) {
                return false;
            }
        }
    }

    return true;
}

static bool miso__tilemap_footprint_occupiable(const MisoTileScene *const scene,
                                               const MisoTilemap *const tilemap,
                                               const MisoTileFootprint *const footprint,
                                               const int tile_x,
                                               const int tile_y) {
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    miso__tile_footprint_bounds(tile_x, tile_y, footprint, &min_x, &min_y, &max_x, &max_y);
    return miso__tilemap_footprint_has_terrain(scene, tilemap, min_x, min_y, max_x, max_y);
}

static void miso__tile_scene_apply_footprint_occupancy(MisoTileScene *const scene,
                                                       const int tile_x,
                                                       const int tile_y,
                                                       const MisoTileFootprint *const footprint,
                                                       const MisoTileOccupancyMask mask,
                                                       const bool occupied) {
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    miso__tile_footprint_bounds(tile_x, tile_y, footprint, &min_x, &min_y, &max_x, &max_y);
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            const size_t idx = miso__tile_scene_index(scene, x, y);
            if (occupied) {
                scene->occupancy[idx] |= mask;
            } else {
                scene->occupancy[idx] &= (Uint8)~mask;
            }
        }
    }
}

static MisoTilePlacementProblem miso__tile_scene_check_footprint(const MisoTileScene *const scene,
                                                                 const MisoTilemap *const tilemap,
                                                                 const int tile_x,
                                                                 const int tile_y,
                                                                 const MisoTileFootprint *const footprint,
                                                                 const MisoTileFlags required_tile_flags,
                                                                 const MisoTileFlags forbidden_tile_flags,
                                                                 const MisoTileOccupancyMask occupied_mask) {
    if (!scene || !tilemap || tilemap->scene != scene || !miso__tile_footprint_valid(footprint)) {
        return MISO_TILE_PLACE_INVALID_ARGUMENT;
    }

    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    miso__tile_footprint_bounds(tile_x, tile_y, footprint, &min_x, &min_y, &max_x, &max_y);

    MisoTilePlacementProblem result = MISO_TILE_PLACE_OK;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            if (!miso__tile_scene_in_bounds(scene, x, y)) {
                result |= MISO_TILE_PLACE_OUT_OF_BOUNDS;
                continue;
            }

            const size_t idx = miso__tile_scene_index(scene, x, y);
            if (tilemap->tiles[idx] == MISO_TILE_EMPTY) {
                result |= MISO_TILE_PLACE_MISSING_TERRAIN;
                continue;
            }
            if (occupied_mask != 0 && (scene->occupancy[idx] & occupied_mask) != 0) {
                result |= MISO_TILE_PLACE_OCCUPIED;
            }
            if (required_tile_flags != 0 && (tilemap->flags[idx] & required_tile_flags) != required_tile_flags) {
                result |= MISO_TILE_PLACE_MISSING_REQUIRED_FLAGS;
            }
            if (forbidden_tile_flags != 0 && (tilemap->flags[idx] & forbidden_tile_flags) != 0) {
                result |= MISO_TILE_PLACE_HAS_FORBIDDEN_FLAGS;
            }
        }
    }

    return result;
}

static bool miso__tile_overlay_sync_gpu(MisoTileOverlay *const overlay) {
    if (!overlay || !overlay->scene || !overlay->rgba8) {
        return false;
    }

    const int width = overlay->scene->map.width_tiles;
    const int height = overlay->scene->map.height_tiles;
    if (!overlay->texture) {
        overlay->texture = miso__renderer_create_rgba8_texture(width, height, overlay->rgba8);
        overlay->dirty = overlay->texture == nullptr;
        return overlay->texture != nullptr;
    }

    if (!overlay->dirty) {
        return true;
    }

    if (!miso__renderer_update_rgba8_texture(overlay->texture, width, height, overlay->rgba8)) {
        return false;
    }
    overlay->dirty = false;
    return true;
}

static void miso__tilemap_rebuild_cache(MisoTilemap *const tilemap, const MisoTileScene *const scene) {
    if (!tilemap || !scene || !tilemap->render_cache) {
        return;
    }

    const float tex_w = (float)((Uint32)tilemap->atlas_columns * (Uint32)scene->map.tile_w_px);
    const float tex_h = (float)((Uint32)tilemap->atlas_rows * (Uint32)scene->map.tile_h_px);
    if (tex_w <= 0.0f || tex_h <= 0.0f) {
        return;
    }

    Uint32 instance_count = 0;
    for (int y = 0; y < scene->map.height_tiles; y++) {
        for (int x = 0; x < scene->map.width_tiles; x++) {
            const size_t idx = miso__tile_scene_index(scene, x, y);
            const Uint32 tile_id = tilemap->tiles[idx];
            if (!miso__tilemap_valid_tile_id(tilemap, tile_id) || tile_id == MISO_TILE_EMPTY) {
                continue;
            }
            const Uint32 col = tile_id % tilemap->atlas_columns;
            const Uint32 row = tile_id / tilemap->atlas_columns;
            float world_x = 0.0f;
            float world_y = 0.0f;
            miso_iso_tile_to_world(&scene->map, x, y, &world_x, &world_y);

            tilemap->render_cache[instance_count++] = (MisoSpriteInstance){
                .x = world_x,
                .y = world_y,
                .z = miso_tile_scene_depth_at_tile(scene, (float)x, (float)y),
                .flags = (tilemap->flags[idx] & MISO_TILE_FLAG_WATER) ? 1.0f : 0.0f,
                .w = (float)scene->map.tile_w_px,
                .h = (float)scene->map.tile_h_px,
                .tile_x = (float)x,
                .tile_y = (float)y,
                .u = ((float)col * (float)scene->map.tile_w_px) / tex_w,
                .v = ((float)row * (float)scene->map.tile_h_px) / tex_h,
                .uw = (float)scene->map.tile_w_px / tex_w,
                .vh = (float)scene->map.tile_h_px / tex_h,
            };
        }
    }

    tilemap->render_cache_count = instance_count;
    tilemap->cache_dirty = false;
}

MisoTileScene *miso_tile_scene_create(MisoEngine *const engine, const MisoTileSceneDesc *const desc) {
    if (!engine || !miso__tile_scene_valid_desc(desc)) {
        return nullptr;
    }

    MisoTileScene *const scene = SDL_calloc(1, sizeof(MisoTileScene));
    if (!scene) {
        return nullptr;
    }

    scene->engine = engine;
    scene->map = desc->map;
    scene->next_object_id = 1;

    scene->occupancy = SDL_calloc(miso__tile_scene_tile_count(scene), sizeof(Uint8));
    if (!scene->occupancy) {
        SDL_free(scene);
        return nullptr;
    }

    return scene;
}

void miso_tile_scene_destroy(MisoTileScene *const scene) {
    if (!scene) {
        return;
    }

    SDL_free(scene->objects);
    SDL_free(scene->object_indices_by_id);
    SDL_free(scene->visuals);
    SDL_free(scene->object_render_cache);
    SDL_free(scene->occupancy);
    SDL_free(scene);
}

const MisoIsoMapDesc *miso_tile_scene_get_desc(const MisoTileScene *const scene) {
    return scene ? &scene->map : nullptr;
}

MisoTilemap *miso_tilemap_create(MisoTileScene *const scene, const MisoTilemapDesc *const desc) {
    if (!scene || !desc || desc->texture == 0) {
        return nullptr;
    }

    Uint16 atlas_columns = 0;
    Uint16 atlas_rows = 0;
    if (!miso__tile_scene_resolve_atlas_grid(
            scene, desc->texture, desc->atlas_columns, desc->atlas_rows, &atlas_columns, &atlas_rows)) {
        return nullptr;
    }

    MisoTilemap *const tilemap = SDL_calloc(1, sizeof(MisoTilemap));
    if (!tilemap) {
        return nullptr;
    }

    const size_t tile_count = miso__tile_scene_tile_count(scene);
    tilemap->tiles = SDL_malloc(tile_count * sizeof(Uint32));
    tilemap->flags = SDL_calloc(tile_count, sizeof(Uint32));
    tilemap->render_cache = SDL_calloc(tile_count, sizeof(MisoSpriteInstance));
    if (!tilemap->tiles || !tilemap->flags || !tilemap->render_cache) {
        miso_tilemap_destroy(tilemap);
        return nullptr;
    }
    SDL_memset4(tilemap->tiles, MISO_TILE_EMPTY, tile_count);

    tilemap->scene = scene;
    tilemap->texture = desc->texture;
    tilemap->atlas_columns = atlas_columns;
    tilemap->atlas_rows = atlas_rows;
    tilemap->cache_dirty = true;
    return tilemap;
}

void miso_tilemap_destroy(MisoTilemap *const tilemap) {
    if (!tilemap) {
        return;
    }

    SDL_free(tilemap->render_cache);
    SDL_free(tilemap->flags);
    SDL_free(tilemap->tiles);
    SDL_free(tilemap);
}

MisoTileOverlay *miso_tile_overlay_create(MisoTileScene *const scene, const MisoTileOverlayDesc *const desc) {
    if (!scene) {
        return nullptr;
    }

    MisoTileOverlay *const overlay = SDL_calloc(1, sizeof(MisoTileOverlay));
    if (!overlay) {
        return nullptr;
    }

    overlay->scene = scene;
    overlay->rgba8 = SDL_malloc(miso__tile_scene_tile_count(scene) * 4U);
    if (!overlay->rgba8) {
        SDL_free(overlay);
        return nullptr;
    }

    miso_tile_overlay_clear(overlay, desc ? desc->clear_rgba8 : 0x00000000U);
    return overlay;
}

void miso_tile_overlay_destroy(MisoTileOverlay *const overlay) {
    if (!overlay) {
        return;
    }

    if (overlay->texture) {
        miso__renderer_destroy_texture(overlay->texture);
    }
    SDL_free(overlay->rgba8);
    SDL_free(overlay);
}

void miso_tile_overlay_clear(MisoTileOverlay *const overlay, const Uint32 rgba8) {
    if (!overlay || !overlay->scene || !overlay->rgba8) {
        return;
    }

    const size_t tile_count = miso__tile_scene_tile_count(overlay->scene);
    for (size_t i = 0; i < tile_count; i++) {
        miso__tile_overlay_store_rgba8(&overlay->rgba8[i * 4U], rgba8);
    }
    overlay->dirty = true;
}

bool miso_tile_overlay_set_tile_rgba8(MisoTileOverlay *const overlay, const int tx, const int ty, const Uint32 rgba8) {
    if (!overlay || !miso__tile_scene_in_bounds(overlay->scene, tx, ty)) {
        return false;
    }

    const size_t idx = miso__tile_scene_index(overlay->scene, tx, ty);
    miso__tile_overlay_store_rgba8(&overlay->rgba8[idx * 4U], rgba8);
    overlay->dirty = true;
    return true;
}

Uint32 miso_tile_overlay_get_tile_rgba8(const MisoTileOverlay *const overlay, const int tx, const int ty) {
    if (!overlay || !miso__tile_scene_in_bounds(overlay->scene, tx, ty)) {
        return 0;
    }

    const size_t idx = miso__tile_scene_index(overlay->scene, tx, ty);
    return miso__tile_overlay_load_rgba8(&overlay->rgba8[idx * 4U]);
}

void miso_tile_overlay_fill_footprint(MisoTileOverlay *const overlay,
                                      const int tile_x,
                                      const int tile_y,
                                      const MisoTileFootprint footprint,
                                      const Uint32 rgba8) {
    if (!overlay || !miso__tile_footprint_valid(&footprint)) {
        return;
    }

    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    miso__tile_footprint_bounds(tile_x, tile_y, &footprint, &min_x, &min_y, &max_x, &max_y);
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            (void)miso_tile_overlay_set_tile_rgba8(overlay, x, y, rgba8);
        }
    }
}

void miso_tilemap_set_tint_overlay(MisoTilemap *const tilemap, MisoTileOverlay *const overlay, const float strength) {
    if (!tilemap || (overlay && overlay->scene != tilemap->scene)) {
        return;
    }

    tilemap->tint_overlay = overlay;
    tilemap->tint_overlay_strength = SDL_clamp(strength, 0.0f, 1.0f);
}

bool miso_tilemap_set_tile(MisoTilemap *const tilemap, const int tx, const int ty, const Uint32 tile_id) {
    if (!tilemap || !miso__tile_scene_in_bounds(tilemap->scene, tx, ty) ||
        !miso__tilemap_valid_tile_id(tilemap, tile_id)) {
        return false;
    }

    const size_t idx = miso__tile_scene_index(tilemap->scene, tx, ty);
    tilemap->tiles[idx] = tile_id;
    if (tile_id == MISO_TILE_EMPTY) {
        tilemap->flags[idx] = MISO_TILE_FLAG_NONE;
    }
    tilemap->cache_dirty = true;
    return true;
}

Uint32 miso_tilemap_get_tile(const MisoTilemap *const tilemap, const int tx, const int ty) {
    if (!tilemap || !miso__tile_scene_in_bounds(tilemap->scene, tx, ty)) {
        return MISO_TILE_EMPTY;
    }

    return tilemap->tiles[miso__tile_scene_index(tilemap->scene, tx, ty)];
}

bool miso_tilemap_has_tile(const MisoTilemap *const tilemap, const int tx, const int ty) {
    return tilemap && miso__tile_scene_in_bounds(tilemap->scene, tx, ty) &&
           tilemap->tiles[miso__tile_scene_index(tilemap->scene, tx, ty)] != MISO_TILE_EMPTY;
}

bool miso_tilemap_clear_tile(MisoTilemap *const tilemap, const int tx, const int ty) {
    return miso_tilemap_set_tile(tilemap, tx, ty, MISO_TILE_EMPTY);
}

bool miso_tilemap_set_flags(MisoTilemap *const tilemap, const int tx, const int ty, const Uint32 flags) {
    if (!tilemap || !miso__tile_scene_in_bounds(tilemap->scene, tx, ty)) {
        return false;
    }

    tilemap->flags[miso__tile_scene_index(tilemap->scene, tx, ty)] = flags;
    tilemap->cache_dirty = true;
    return true;
}

Uint32 miso_tilemap_get_flags(const MisoTilemap *const tilemap, const int tx, const int ty) {
    if (!tilemap || !miso__tile_scene_in_bounds(tilemap->scene, tx, ty)) {
        return 0;
    }

    return tilemap->flags[miso__tile_scene_index(tilemap->scene, tx, ty)];
}

void miso_tilemap_fill(MisoTilemap *const tilemap, const Uint32 tile_id, const Uint32 flags) {
    if (!tilemap || !tilemap->scene || !miso__tilemap_valid_tile_id(tilemap, tile_id)) {
        return;
    }

    const size_t tile_count = miso__tile_scene_tile_count(tilemap->scene);
    for (size_t i = 0; i < tile_count; i++) {
        tilemap->tiles[i] = tile_id;
        tilemap->flags[i] = tile_id == MISO_TILE_EMPTY ? MISO_TILE_FLAG_NONE : flags;
    }
    tilemap->cache_dirty = true;
}

void miso_tilemap_clear(MisoTilemap *const tilemap) {
    miso_tilemap_fill(tilemap, MISO_TILE_EMPTY, MISO_TILE_FLAG_NONE);
}

MisoTilePlacementProblem miso_tile_scene_check_placement(const MisoTileScene *const scene,
                                                         const MisoTilemap *const tilemap,
                                                         const MisoTilePlacementQuery *const query) {
    if (!scene || !tilemap || tilemap->scene != scene || !query || !miso__tile_footprint_valid(&query->footprint)) {
        return MISO_TILE_PLACE_INVALID_ARGUMENT;
    }

    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    miso__tile_footprint_bounds(query->tile_x, query->tile_y, &query->footprint, &min_x, &min_y, &max_x, &max_y);

    MisoTilePlacementProblem result = miso__tile_scene_check_footprint(scene,
                                                                       tilemap,
                                                                       query->tile_x,
                                                                       query->tile_y,
                                                                       &query->footprint,
                                                                       query->required_tile_flags,
                                                                       query->forbidden_tile_flags,
                                                                       query->occupied_mask);

    if ((result & MISO_TILE_PLACE_OUT_OF_BOUNDS) == 0 &&
        !miso__tile_scene_has_adjacent_flags(
            scene, tilemap, min_x, min_y, max_x, max_y, query->required_adjacent_tile_flags)) {
        result |= MISO_TILE_PLACE_MISSING_ADJACENCY;
    }

    return result;
}

MisoResult miso_tile_scene_place_object(MisoTileScene *const scene,
                                        const MisoTilemap *const tilemap,
                                        const MisoTileObjectDesc *const desc,
                                        MisoTileObjectId *const out_id) {
    if (scene) {
        scene->stats.place_calls++;
    }
    if (!scene || !tilemap || tilemap->scene != scene || !desc || !miso__tile_footprint_valid(&desc->footprint)) {
        return MISO_ERR_INVALID_ARG;
    }
    if (!miso__tilemap_footprint_occupiable(scene, tilemap, &desc->footprint, desc->tile_x, desc->tile_y)) {
        return MISO_ERR_INVALID_ARG;
    }

    const MisoTilePlacementQuery query = {
        .tile_x = desc->tile_x,
        .tile_y = desc->tile_y,
        .footprint = desc->footprint,
        .forbidden_tile_flags = MISO_TILE_FLAG_BLOCKS_OBJECTS,
        .occupied_mask = desc->occupancy_mask == 0 ? MISO_TILE_OCCUPANCY_OBJECT : desc->occupancy_mask,
    };
    if (miso_tile_scene_check_placement(scene, tilemap, &query) != MISO_TILE_PLACE_OK) {
        return MISO_ERR_INVALID_ARG;
    }

    if (!miso__tile_scene_ensure_object_capacity(scene) ||
        !miso__tile_scene_ensure_object_index_capacity(scene, scene->next_object_id)) {
        return MISO_ERR_OUT_OF_MEMORY;
    }

    const Uint32 object_index = scene->object_count;
    MisoTileObjectRecord *const record = &scene->objects[scene->object_count++];
    record->id = scene->next_object_id++;
    record->type_id = desc->type_id;
    record->tile_x = desc->tile_x;
    record->tile_y = desc->tile_y;
    record->footprint = desc->footprint;
    record->visual_id = desc->visual_id;
    record->occupancy_mask = query.occupied_mask;
    record->pickable = desc->pickable;
    record->game_ref = desc->game_ref;
    record->active = true;
    scene->active_object_count++;
    scene->object_indices_by_id[record->id] = object_index;
    miso__tile_scene_apply_footprint_occupancy(
        scene, desc->tile_x, desc->tile_y, &desc->footprint, record->occupancy_mask, true);

    if (out_id) {
        *out_id = record->id;
    }

    return MISO_OK;
}

MisoResult miso_tile_scene_remove_object(MisoTileScene *const scene, const MisoTileObjectId object_id) {
    if (scene) {
        scene->stats.remove_calls++;
    }
    if (!scene || object_id == 0) {
        return MISO_ERR_INVALID_ARG;
    }

    Uint32 object_index = MISO_TILE_OBJECT_INDEX_INVALID;
    MisoTileObjectRecord *const record = miso__tile_scene_find_object(scene, object_id, &object_index);
    if (!record) {
        return MISO_ERR_NOT_FOUND;
    }

    miso__tile_scene_apply_footprint_occupancy(
        scene, record->tile_x, record->tile_y, &record->footprint, record->occupancy_mask, false);

    const Uint32 last_index = scene->object_count - 1U;
    scene->object_indices_by_id[object_id] = MISO_TILE_OBJECT_INDEX_INVALID;
    if (object_index != last_index) {
        scene->objects[object_index] = scene->objects[last_index];
        scene->object_indices_by_id[scene->objects[object_index].id] = object_index;
    }
    SDL_memset(&scene->objects[last_index], 0, sizeof(scene->objects[last_index]));
    scene->object_count--;
    scene->active_object_count = scene->object_count;
    return MISO_OK;
}

MisoResult miso_tile_scene_move_object(MisoTileScene *const scene,
                                       const MisoTilemap *const tilemap,
                                       const MisoTileMoveQuery *const query,
                                       MisoTileMoveResult *const out_result) {
    if (out_result) {
        *out_result = (MisoTileMoveResult){0};
    }
    if (scene) {
        scene->stats.move_calls++;
    }
    if (!scene || !tilemap || tilemap->scene != scene || !query || query->object_id == 0) {
        if (out_result) {
            out_result->problems = MISO_TILE_PLACE_INVALID_ARGUMENT;
        }
        if (scene) {
            scene->stats.move_failures++;
        }
        return MISO_ERR_INVALID_ARG;
    }

    MisoTileObjectRecord *const record = miso__tile_scene_find_object(scene, query->object_id, nullptr);
    if (!record) {
        if (out_result) {
            out_result->problems = MISO_TILE_PLACE_INVALID_ARGUMENT;
        }
        scene->stats.move_failures++;
        return MISO_ERR_NOT_FOUND;
    }

    if (record->tile_x == query->tile_x && record->tile_y == query->tile_y) {
        scene->stats.move_successes++;
        return MISO_OK;
    }

    const MisoTileOccupancyMask blocked_occupancy_mask =
        query->blocked_occupancy_mask != 0 ? query->blocked_occupancy_mask : record->occupancy_mask;

    miso__tile_scene_apply_footprint_occupancy(
        scene, record->tile_x, record->tile_y, &record->footprint, record->occupancy_mask, false);
    const MisoTilePlacementProblem problems = miso__tile_scene_check_footprint(scene,
                                                                               tilemap,
                                                                               query->tile_x,
                                                                               query->tile_y,
                                                                               &record->footprint,
                                                                               query->required_tile_flags,
                                                                               query->forbidden_tile_flags,
                                                                               blocked_occupancy_mask);
    if (problems != MISO_TILE_PLACE_OK) {
        miso__tile_scene_apply_footprint_occupancy(
            scene, record->tile_x, record->tile_y, &record->footprint, record->occupancy_mask, true);
        if (out_result) {
            out_result->problems = problems;
        }
        scene->stats.move_failures++;
        return MISO_ERR_INVALID_ARG;
    }

    record->tile_x = query->tile_x;
    record->tile_y = query->tile_y;
    miso__tile_scene_apply_footprint_occupancy(
        scene, record->tile_x, record->tile_y, &record->footprint, record->occupancy_mask, true);
    if (out_result) {
        out_result->problems = MISO_TILE_PLACE_OK;
    }
    scene->stats.move_successes++;
    return MISO_OK;
}

void miso_tile_scene_clear_objects(MisoTileScene *const scene) {
    if (!scene) {
        return;
    }

    SDL_memset(scene->occupancy, 0, miso__tile_scene_tile_count(scene) * sizeof(Uint8));
    if (scene->objects) {
        SDL_memset(scene->objects, 0, sizeof(MisoTileObjectRecord) * scene->object_capacity);
    }
    if (scene->object_indices_by_id) {
        SDL_memset4(scene->object_indices_by_id, MISO_TILE_OBJECT_INDEX_INVALID, scene->object_index_capacity);
    }
    scene->object_count = 0;
    scene->active_object_count = 0;
    scene->next_object_id = 1;
}

bool miso_tile_scene_pick_object_at_tile(const MisoTileScene *const scene,
                                         const int tx,
                                         const int ty,
                                         MisoTileObjectId *const out_id) {
    if (!scene || !out_id || !miso__tile_scene_in_bounds(scene, tx, ty)) {
        return false;
    }

    for (Uint32 i = scene->object_count; i > 0; i--) {
        const MisoTileObjectRecord *const record = &scene->objects[i - 1];
        if (!record->active || !record->pickable) {
            continue;
        }

        int min_x = 0;
        int min_y = 0;
        int max_x = 0;
        int max_y = 0;
        miso__tile_footprint_bounds(record->tile_x, record->tile_y, &record->footprint, &min_x, &min_y, &max_x, &max_y);
        if (tx >= min_x && ty >= min_y && tx <= max_x && ty <= max_y) {
            *out_id = record->id;
            return true;
        }
    }

    return false;
}

bool miso_tile_scene_pick_object_at_screen(const MisoTileScene *const scene,
                                           const MisoEngine *const engine,
                                           const MisoCameraId camera_id,
                                           const int sx,
                                           const int sy,
                                           MisoTileObjectId *const out_id) {
    if (!scene || !engine || !out_id) {
        return false;
    }

    const MisoVec2 world = miso_camera_screen_to_world(engine, camera_id, sx, sy);
    const MisoIsoTileCoordF tile = miso_iso_world_to_tile_f(&scene->map, world.x, world.y);
    return miso_tile_scene_pick_object_at_tile(scene, (int)SDL_floorf(tile.x), (int)SDL_floorf(tile.y), out_id);
}

int miso_tile_scene_get_objects(const MisoTileScene *const scene,
                                MisoTileObjectInfo *const out_items,
                                const int capacity) {
    if (!scene || !out_items || capacity <= 0) {
        return 0;
    }

    int written = 0;
    for (Uint32 i = 0; i < scene->object_count && written < capacity; i++) {
        const MisoTileObjectRecord *const record = &scene->objects[i];
        if (!record->active) {
            continue;
        }

        out_items[written++] = (MisoTileObjectInfo){
            .id = record->id,
            .type_id = record->type_id,
            .tile_x = record->tile_x,
            .tile_y = record->tile_y,
            .footprint = record->footprint,
            .visual_id = record->visual_id,
            .occupancy_mask = record->occupancy_mask,
            .pickable = record->pickable,
            .game_ref = record->game_ref,
        };
    }

    return written;
}

bool miso_tile_scene_get_stats(const MisoTileScene *const scene, MisoTileSceneStats *const out_stats) {
    if (!scene || !out_stats) {
        return false;
    }

    *out_stats = scene->stats;
    out_stats->active_object_count = scene->active_object_count;
    out_stats->object_storage_count = scene->object_count;
    out_stats->object_capacity = scene->object_capacity;
    return true;
}

void miso_tile_scene_reset_stats(MisoTileScene *const scene) {
    if (!scene) {
        return;
    }

    scene->stats = (MisoTileSceneStats){0};
}

MisoResult miso_tile_scene_set_object_visual(MisoTileScene *const scene, const MisoTileObjectVisualDesc *const desc) {
    if (!scene || !desc || desc->visual_id == 0 || desc->texture == 0 || desc->sprite_w_tiles <= 0 ||
        desc->sprite_h_tiles <= 0) {
        return MISO_ERR_INVALID_ARG;
    }

    Uint16 atlas_columns = 0;
    Uint16 atlas_rows = 0;
    if (!miso__tile_scene_resolve_atlas_grid(
            scene, desc->texture, desc->atlas_columns, desc->atlas_rows, &atlas_columns, &atlas_rows)) {
        return MISO_ERR_INVALID_ARG;
    }

    MisoTileObjectVisualRecord *visual = miso__tile_scene_find_visual(scene, desc->visual_id);
    if (!visual) {
        if (!miso__tile_scene_ensure_visual_capacity(scene)) {
            return MISO_ERR_OUT_OF_MEMORY;
        }
        visual = &scene->visuals[scene->visual_count++];
    }

    *visual = (MisoTileObjectVisualRecord){
        .visual_id = desc->visual_id,
        .texture = desc->texture,
        .atlas_columns = atlas_columns,
        .atlas_rows = atlas_rows,
        .atlas_tile_id = desc->atlas_tile_id,
        .sprite_w_tiles = desc->sprite_w_tiles,
        .sprite_h_tiles = desc->sprite_h_tiles,
    };
    return MISO_OK;
}

void miso_tile_scene_render_objects(const MisoEngine *const engine,
                                    MisoTileScene *const scene,
                                    const MisoCameraId camera_id) {
    if (!engine || !scene || scene->object_count == 0 ||
        !miso__tile_scene_ensure_object_render_capacity(scene, scene->object_count)) {
        return;
    }

    const float tile_w = (float)scene->map.tile_w_px;
    const float tile_h = (float)scene->map.tile_h_px;
    const float iso_w = tile_w;
    const float iso_h = tile_h * 0.5f;
    const float start_x = (float)(scene->map.height_tiles - 1) * iso_w * 0.5f;
    const float texel_w = tile_w;
    const float texel_h = tile_h;

    miso_render_begin_world(engine, camera_id);

    MisoTextureHandle active_texture = 0;
    Uint32 instance_count = 0;

    for (Uint32 i = 0; i < scene->object_count; i++) {
        const MisoTileObjectRecord *const object = &scene->objects[i];
        if (!object->active) {
            continue;
        }

        const MisoTileObjectVisualRecord *const visual = miso__tile_scene_find_visual_const(scene, object->visual_id);
        if (!visual) {
            continue;
        }

        if (active_texture != 0 && active_texture != visual->texture && instance_count > 0) {
            miso_render_submit_sprites(engine, active_texture, scene->object_render_cache, (int)instance_count);
            instance_count = 0;
        }
        active_texture = visual->texture;

        const Uint32 col = visual->atlas_tile_id % visual->atlas_columns;
        const Uint32 row = visual->atlas_tile_id / visual->atlas_columns;
        const float tex_w = (float)((Uint32)visual->atlas_columns * (Uint32)scene->map.tile_w_px);
        const float tex_h = (float)((Uint32)visual->atlas_rows * (Uint32)scene->map.tile_h_px);
        const float sprite_w = (float)visual->sprite_w_tiles * tile_w;
        const float sprite_h = (float)visual->sprite_h_tiles * tile_h;

        float world_x = start_x + (float)(object->tile_x - object->tile_y) * (iso_w * 0.5f);
        float world_y = (float)(object->tile_x + object->tile_y) * (iso_h * 0.5f);
        world_y -= tile_h;
        world_y -= (float)visual->sprite_h_tiles * iso_h;
        world_x -= (float)(object->footprint.width - 1) * 0.5f * iso_w;

        scene->object_render_cache[instance_count++] = (MisoSpriteInstance){
            .x = world_x,
            .y = world_y,
            .z = miso_tile_scene_depth_at_tile(scene, (float)object->tile_x, (float)object->tile_y) - 0.001f,
            .flags = 0.0f,
            .w = sprite_w,
            .h = sprite_h,
            .tile_x = (float)object->tile_x,
            .tile_y = (float)object->tile_y,
            .u = ((float)col * texel_w) / tex_w,
            .v = ((float)row * texel_h) / tex_h,
            .uw = sprite_w / tex_w,
            .vh = sprite_h / tex_h,
        };
    }

    if (active_texture != 0 && instance_count > 0) {
        miso_render_submit_sprites(engine, active_texture, scene->object_render_cache, (int)instance_count);
    }

    miso_render_end_world(engine);
}

bool miso_tile_scene_tile_to_world(const MisoTileScene *const scene,
                                   const float tile_x,
                                   const float tile_y,
                                   float *const restrict out_world_x,
                                   float *const restrict out_world_y) {
    if (!scene || !out_world_x || !out_world_y) {
        return false;
    }

    const float iso_w = (float)scene->map.tile_w_px;
    const float iso_h = (float)scene->map.tile_h_px * 0.5f;
    const float start_x = ((float)(scene->map.height_tiles - 1) * iso_w) * 0.5f;
    const float origin_x = start_x + iso_w * 0.5f;
    *out_world_x = origin_x + (tile_x - tile_y) * (iso_w * 0.5f);
    *out_world_y = (tile_x + tile_y) * (iso_h * 0.5f);
    return true;
}

bool miso_tile_scene_world_to_tile(const MisoTileScene *const scene,
                                   const float world_x,
                                   const float world_y,
                                   float *const restrict out_tile_x,
                                   float *const restrict out_tile_y) {
    if (!scene || !out_tile_x || !out_tile_y) {
        return false;
    }

    const MisoIsoTileCoordF tile = miso_iso_world_to_tile_f(&scene->map, world_x, world_y);
    *out_tile_x = tile.x;
    *out_tile_y = tile.y;
    return true;
}

float miso_tile_scene_depth_at_tile(const MisoTileScene *const scene, const float tile_x, const float tile_y) {
    if (!scene || scene->map.width_tiles <= 0 || scene->map.height_tiles <= 0) {
        return 0.0f;
    }

    return 1.0f - (tile_x + tile_y) / (float)(scene->map.width_tiles + scene->map.height_tiles);
}

void miso_tilemap_render(const MisoEngine *const engine,
                         MisoTilemap *const tilemap,
                         const MisoTileScene *const scene,
                         const MisoCameraId camera_id) {
    if (!engine || !tilemap || !scene || tilemap->scene != scene) {
        return;
    }

    if (tilemap->cache_dirty) {
        miso__tilemap_rebuild_cache(tilemap, scene);
    }
    if (tilemap->render_cache_count == 0) {
        return;
    }

    if (tilemap->tint_overlay && !miso__tile_overlay_sync_gpu(tilemap->tint_overlay)) {
        return;
    }

    miso_render_begin_world(engine, camera_id);
    if (tilemap->tint_overlay && tilemap->tint_overlay->texture && tilemap->tint_overlay_strength > 0.0f) {
        miso__renderer_set_sprite_tint_overlay(tilemap->tint_overlay->texture,
                                               scene->map.width_tiles,
                                               scene->map.height_tiles,
                                               tilemap->tint_overlay_strength);
    }
    miso_render_submit_sprites(engine, tilemap->texture, tilemap->render_cache, (int)tilemap->render_cache_count);
    miso__renderer_set_sprite_tint_overlay(nullptr, 0, 0, 0.0f);
    miso_render_end_world(engine);
}
