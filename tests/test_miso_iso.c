#include "miso_iso.h"
#include "miso_tile_scene.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

typedef struct IsoTileSample {
    int tile_x;
    int tile_y;
    float world_x;
    float world_y;
} IsoTileSample;

typedef struct IsoFloatSample {
    float tile_x;
    float tile_y;
    int expected_floor_x;
    int expected_floor_y;
} IsoFloatSample;

static const MisoIsoMapDesc g_desc = {
    .width_tiles = 10,
    .height_tiles = 10,
    .tile_w_px = 32,
    .tile_h_px = 32,
};

static int failf(const char *fmt, ...) {
    va_list args = nullptr;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    return 1;
}

static bool nearly_equal(const float a, const float b) {
    return SDL_fabsf(a - b) <= 0.001f;
}

static void tile_f_to_world(const MisoIsoMapDesc *const desc,
                            const float tile_x,
                            const float tile_y,
                            float *const out_world_x,
                            float *const out_world_y) {
    const float iso_w = (float)desc->tile_w_px;
    const float iso_h = (float)desc->tile_h_px * 0.5f;
    const float start_x = ((float)(desc->height_tiles - 1) * iso_w) * 0.5f;
    const float origin_x = start_x + iso_w * 0.5f;
    const float half_iso_w = iso_w * 0.5f;
    const float half_iso_h = iso_h * 0.5f;

    *out_world_x = origin_x + (tile_x - tile_y) * half_iso_w;
    *out_world_y = (tile_x + tile_y) * half_iso_h;
}

static int run_tile_to_world_known_case(void) {
    static const IsoTileSample samples[] = {
        {.tile_x = 0, .tile_y = 0, .world_x = 144.0f, .world_y = 0.0f},
        {.tile_x = 1, .tile_y = 0, .world_x = 160.0f, .world_y = 8.0f},
        {.tile_x = 0, .tile_y = 1, .world_x = 128.0f, .world_y = 8.0f},
        {.tile_x = 3, .tile_y = 2, .world_x = 160.0f, .world_y = 40.0f},
        {.tile_x = 5, .tile_y = 4, .world_x = 160.0f, .world_y = 72.0f},
    };

    for (size_t i = 0; i < SDL_arraysize(samples); i++) {
        const IsoTileSample sample = samples[i];
        float world_x = 0.0f;
        float world_y = 0.0f;
        miso_iso_tile_to_world(&g_desc, sample.tile_x, sample.tile_y, &world_x, &world_y);

        if (!nearly_equal(world_x, sample.world_x) || !nearly_equal(world_y, sample.world_y)) {
            return failf("tile (%d, %d) mapped to world (%.3f, %.3f), expected (%.3f, %.3f)",
                         sample.tile_x,
                         sample.tile_y,
                         world_x,
                         world_y,
                         sample.world_x,
                         sample.world_y);
        }
    }

    return 0;
}

static int run_world_to_tile_center_case(void) {
    static const IsoTileSample samples[] = {
        {.tile_x = 0, .tile_y = 0},
        {.tile_x = 1, .tile_y = 0},
        {.tile_x = 0, .tile_y = 1},
        {.tile_x = 3, .tile_y = 2},
        {.tile_x = 5, .tile_y = 4},
    };

    for (size_t i = 0; i < SDL_arraysize(samples); i++) {
        const IsoTileSample sample = samples[i];
        float world_x = 0.0f;
        float world_y = 0.0f;
        miso_iso_tile_to_world(&g_desc, sample.tile_x, sample.tile_y, &world_x, &world_y);
        world_x += (float)g_desc.tile_w_px * 0.5f;
        world_y += ((float)g_desc.tile_h_px * 0.5f) * 0.5f;

        const MisoIsoTileCoordF tile = miso_iso_world_to_tile_f(&g_desc, world_x, world_y);
        int floor_x = -1;
        int floor_y = -1;
        miso_iso_world_to_tile_floor(&g_desc, world_x, world_y, &floor_x, &floor_y);

        if (!nearly_equal(tile.x, (float)sample.tile_x + 0.5f) || !nearly_equal(tile.y, (float)sample.tile_y + 0.5f)) {
            return failf("tile center (%d, %d) inverted to tile float (%.3f, %.3f)",
                         sample.tile_x,
                         sample.tile_y,
                         tile.x,
                         tile.y);
        }
        if (floor_x != sample.tile_x || floor_y != sample.tile_y) {
            return failf("tile center (%d, %d) floored to (%d, %d)", sample.tile_x, sample.tile_y, floor_x, floor_y);
        }
    }

    return 0;
}

static int run_world_to_tile_floor_boundary_case(void) {
    static const IsoFloatSample samples[] = {
        {.tile_x = 2.001f, .tile_y = 3.001f, .expected_floor_x = 2, .expected_floor_y = 3},
        {.tile_x = 2.999f, .tile_y = 3.999f, .expected_floor_x = 2, .expected_floor_y = 3},
        {.tile_x = 3.000f, .tile_y = 4.000f, .expected_floor_x = 3, .expected_floor_y = 4},
        {.tile_x = -0.001f, .tile_y = 0.500f, .expected_floor_x = -1, .expected_floor_y = 0},
        {.tile_x = 0.500f, .tile_y = -0.001f, .expected_floor_x = 0, .expected_floor_y = -1},
    };

    for (size_t i = 0; i < SDL_arraysize(samples); i++) {
        const IsoFloatSample sample = samples[i];
        float world_x = 0.0f;
        float world_y = 0.0f;
        tile_f_to_world(&g_desc, sample.tile_x, sample.tile_y, &world_x, &world_y);

        int floor_x = 0;
        int floor_y = 0;
        miso_iso_world_to_tile_floor(&g_desc, world_x, world_y, &floor_x, &floor_y);

        if (floor_x != sample.expected_floor_x || floor_y != sample.expected_floor_y) {
            return failf("tile float (%.3f, %.3f) via world (%.3f, %.3f) floored to (%d, %d), expected (%d, %d)",
                         sample.tile_x,
                         sample.tile_y,
                         world_x,
                         world_y,
                         floor_x,
                         floor_y,
                         sample.expected_floor_x,
                         sample.expected_floor_y);
        }
    }

    return 0;
}

static int run_tile_scene_placement_case(void) {
    const MisoTileSceneDesc scene_desc = {
        .map = g_desc,
    };
    MisoTileScene *const scene = miso_tile_scene_create((MisoEngine *)(uintptr_t)1, &scene_desc);
    if (!scene) {
        return failf("failed to create tile scene");
    }

    const MisoTilemapDesc tilemap_desc = {
        .texture = 1,
        .atlas_columns = 4,
        .atlas_rows = 4,
    };
    MisoTilemap *const tilemap = miso_tilemap_create(scene, &tilemap_desc);
    if (!tilemap) {
        miso_tile_scene_destroy(scene);
        return failf("failed to create tilemap");
    }

    if (miso_tilemap_get_tile(tilemap, 0, 0) != MISO_TILE_EMPTY || miso_tilemap_has_tile(tilemap, 0, 0)) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("expected new tilemap cells to initialize empty");
    }

    miso_tilemap_fill(tilemap, 0, MISO_TILE_FLAG_BUILDABLE | MISO_TILE_FLAG_WALKABLE);
    miso_tilemap_set_flags(tilemap, 4, 2, MISO_TILE_FLAG_PATH | MISO_TILE_FLAG_WALKABLE);

    const MisoTileFootprint footprint = {
        .width = 2,
        .height = 2,
        .anchor_x = 0,
        .anchor_y = 0,
    };
    const MisoTilePlacementQuery query = {
        .tile_x = 2,
        .tile_y = 2,
        .footprint = footprint,
        .required_tile_flags = MISO_TILE_FLAG_BUILDABLE,
        .forbidden_tile_flags = MISO_TILE_FLAG_WATER,
        .occupied_mask = MISO_TILE_OCCUPANCY_OBJECT,
        .required_adjacent_tile_flags = MISO_TILE_FLAG_PATH,
    };

    if (miso_tile_scene_check_placement(scene, tilemap, &query) != MISO_TILE_PLACE_OK) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("expected initial tile placement to be valid");
    }

    MisoTileObjectId object_id = 0;
    const MisoTileObjectDesc object = {
        .type_id = 7,
        .tile_x = 2,
        .tile_y = 2,
        .footprint = footprint,
        .visual_id = 3,
        .occupancy_mask = MISO_TILE_OCCUPANCY_OBJECT,
        .pickable = true,
        .game_ref = 99,
    };
    if (miso_tile_scene_place_object(scene, tilemap, &object, &object_id) != MISO_OK || object_id == 0) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("failed to place tile object");
    }

    if ((miso_tile_scene_check_placement(scene, tilemap, &query) & MISO_TILE_PLACE_OCCUPIED) == 0) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("expected occupied placement to report occupancy");
    }

    MisoTileObjectId picked_id = 0;
    if (!miso_tile_scene_pick_object_at_tile(scene, 3, 3, &picked_id) || picked_id != object_id) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("failed to pick placed tile object");
    }

    const MisoTileMoveQuery move_query = {
        .object_id = object_id,
        .tile_x = 5,
        .tile_y = 2,
        .required_tile_flags = MISO_TILE_FLAG_BUILDABLE,
        .forbidden_tile_flags = MISO_TILE_FLAG_WATER,
        .blocked_occupancy_mask = MISO_TILE_OCCUPANCY_OBJECT,
    };
    if (miso_tile_scene_move_object(scene, tilemap, &move_query, nullptr) != MISO_OK) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("failed to move tile object");
    }

    if (miso_tile_scene_check_placement(scene, tilemap, &query) != MISO_TILE_PLACE_OK) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("expected move to clear previous footprint occupancy");
    }

    MisoTilePlacementQuery moved_query = query;
    moved_query.tile_x = 5;
    moved_query.tile_y = 2;
    moved_query.required_adjacent_tile_flags = MISO_TILE_FLAG_NONE;
    if ((miso_tile_scene_check_placement(scene, tilemap, &moved_query) & MISO_TILE_PLACE_OCCUPIED) == 0) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("expected move to occupy destination footprint");
    }

    picked_id = 0;
    if (!miso_tile_scene_pick_object_at_tile(scene, 6, 3, &picked_id) || picked_id != object_id) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("failed to pick moved tile object");
    }

    MisoTileObjectId blocker_id = 0;
    const MisoTileObjectDesc blocker = {
        .type_id = 8,
        .tile_x = 8,
        .tile_y = 2,
        .footprint = {.width = 1, .height = 1, .anchor_x = 0, .anchor_y = 0},
        .visual_id = 3,
        .occupancy_mask = MISO_TILE_OCCUPANCY_OBJECT,
        .pickable = true,
    };
    if (miso_tile_scene_place_object(scene, tilemap, &blocker, &blocker_id) != MISO_OK || blocker_id == 0) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("failed to place move blocker");
    }

    MisoTileMoveResult blocked_move = {0};
    const MisoTileMoveQuery blocked_move_query = {
        .object_id = object_id,
        .tile_x = 7,
        .tile_y = 2,
        .required_tile_flags = MISO_TILE_FLAG_BUILDABLE,
        .forbidden_tile_flags = MISO_TILE_FLAG_WATER,
        .blocked_occupancy_mask = MISO_TILE_OCCUPANCY_OBJECT,
    };
    if (miso_tile_scene_move_object(scene, tilemap, &blocked_move_query, &blocked_move) == MISO_OK ||
        (blocked_move.problems & MISO_TILE_PLACE_OCCUPIED) == 0) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("expected blocked move to fail with occupancy");
    }

    picked_id = 0;
    if (!miso_tile_scene_pick_object_at_tile(scene, 6, 3, &picked_id) || picked_id != object_id) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("expected failed move to restore source occupancy");
    }

    if (miso_tile_scene_remove_object(scene, object_id) != MISO_OK ||
        miso_tile_scene_check_placement(scene, tilemap, &moved_query) != MISO_TILE_PLACE_OK) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("failed to remove object and clear occupancy");
    }

    if (!miso_tilemap_clear_tile(tilemap, 2, 2) || miso_tilemap_has_tile(tilemap, 2, 2) ||
        miso_tilemap_get_tile(tilemap, 2, 2) != MISO_TILE_EMPTY) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("failed to clear tile to empty terrain");
    }

    if ((miso_tile_scene_check_placement(scene, tilemap, &query) & MISO_TILE_PLACE_MISSING_TERRAIN) == 0) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("expected empty tile footprint to report missing terrain");
    }

    if (miso_tile_scene_place_object(scene, tilemap, &object, nullptr) != MISO_ERR_INVALID_ARG) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("expected object placement on empty terrain to fail");
    }

    miso_tilemap_clear(tilemap);
    if (miso_tilemap_get_tile(tilemap, 0, 0) != MISO_TILE_EMPTY ||
        miso_tilemap_get_flags(tilemap, 0, 0) != MISO_TILE_FLAG_NONE) {
        miso_tilemap_destroy(tilemap);
        miso_tile_scene_destroy(scene);
        return failf("failed to clear tilemap to empty terrain");
    }

    miso_tilemap_destroy(tilemap);
    miso_tile_scene_destroy(scene);
    return 0;
}

static int run_tile_scene_fractional_coords_case(void) {
    const MisoTileSceneDesc scene_desc = {
        .map = g_desc,
    };
    MisoTileScene *const scene = miso_tile_scene_create((MisoEngine *)(uintptr_t)1, &scene_desc);
    if (!scene) {
        return failf("failed to create tile scene");
    }

    float world_x = 0.0f;
    float world_y = 0.0f;
    if (!miso_tile_scene_tile_to_world(scene, 2.5f, 3.25f, &world_x, &world_y)) {
        miso_tile_scene_destroy(scene);
        return failf("failed to map fractional tile to world");
    }

    float tile_x = 0.0f;
    float tile_y = 0.0f;
    if (!miso_tile_scene_world_to_tile(scene, world_x, world_y, &tile_x, &tile_y)) {
        miso_tile_scene_destroy(scene);
        return failf("failed to map world to fractional tile");
    }

    if (!nearly_equal(tile_x, 2.5f) || !nearly_equal(tile_y, 3.25f)) {
        miso_tile_scene_destroy(scene);
        return failf("fractional tile roundtrip produced (%.3f, %.3f)", tile_x, tile_y);
    }

    miso_tile_scene_destroy(scene);
    return 0;
}

static int run_tile_overlay_buffer_case(void) {
    const MisoTileSceneDesc scene_desc = {
        .map = g_desc,
    };
    MisoTileScene *const scene = miso_tile_scene_create((MisoEngine *)(uintptr_t)1, &scene_desc);
    if (!scene) {
        return failf("failed to create tile scene");
    }

    const MisoTileOverlayDesc overlay_desc = {
        .clear_rgba8 = 0x00000000u,
    };
    MisoTileOverlay *const overlay = miso_tile_overlay_create(scene, &overlay_desc);
    if (!overlay) {
        miso_tile_scene_destroy(scene);
        return failf("failed to create tile overlay");
    }

    if (miso_tile_overlay_get_tile_rgba8(overlay, 2, 3) != 0x00000000u) {
        miso_tile_overlay_destroy(overlay);
        miso_tile_scene_destroy(scene);
        return failf("overlay did not initialize to clear color");
    }

    if (!miso_tile_overlay_set_tile_rgba8(overlay, 2, 3, 0x00FF00CCu) ||
        miso_tile_overlay_get_tile_rgba8(overlay, 2, 3) != 0x00FF00CCu) {
        miso_tile_overlay_destroy(overlay);
        miso_tile_scene_destroy(scene);
        return failf("overlay set/get tile failed");
    }

    const MisoTileFootprint footprint = {.width = 2, .height = 2, .anchor_x = 0, .anchor_y = 0};
    miso_tile_overlay_fill_footprint(overlay, 4, 4, footprint, 0xFF000080u);
    if (miso_tile_overlay_get_tile_rgba8(overlay, 5, 5) != 0xFF000080u) {
        miso_tile_overlay_destroy(overlay);
        miso_tile_scene_destroy(scene);
        return failf("overlay footprint fill failed");
    }

    miso_tile_overlay_clear(overlay, 0x11223344u);
    if (miso_tile_overlay_get_tile_rgba8(overlay, 5, 5) != 0x11223344u) {
        miso_tile_overlay_destroy(overlay);
        miso_tile_scene_destroy(scene);
        return failf("overlay clear failed");
    }

    miso_tile_overlay_destroy(overlay);
    miso_tile_scene_destroy(scene);
    return 0;
}

int main(const int argc, const char *const *const argv) {
    if (argc != 3 || SDL_strcmp(argv[1], "--case") != 0) {
        fprintf(stderr,
                "usage: %s --case "
                "<tile-to-world|world-to-tile-center|world-to-tile-floor-boundary|tile-scene-placement|"
                "tile-scene-fractional-coords|tile-overlay-buffer>\n",
                argv[0]);
        return 2;
    }

    if (SDL_strcmp(argv[2], "tile-to-world") == 0) {
        return run_tile_to_world_known_case();
    }
    if (SDL_strcmp(argv[2], "world-to-tile-center") == 0) {
        return run_world_to_tile_center_case();
    }
    if (SDL_strcmp(argv[2], "world-to-tile-floor-boundary") == 0) {
        return run_world_to_tile_floor_boundary_case();
    }
    if (SDL_strcmp(argv[2], "tile-scene-placement") == 0) {
        return run_tile_scene_placement_case();
    }
    if (SDL_strcmp(argv[2], "tile-scene-fractional-coords") == 0) {
        return run_tile_scene_fractional_coords_case();
    }
    if (SDL_strcmp(argv[2], "tile-overlay-buffer") == 0) {
        return run_tile_overlay_buffer_case();
    }

    fprintf(stderr, "unknown test case: %s\n", argv[2]);
    return 2;
}
