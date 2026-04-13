#include "miso_iso.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
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

int main(const int argc, const char *const *const argv) {
    if (argc != 3 || SDL_strcmp(argv[1], "--case") != 0) {
        fprintf(
            stderr, "usage: %s --case <tile-to-world|world-to-tile-center|world-to-tile-floor-boundary>\n", argv[0]);
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

    fprintf(stderr, "unknown test case: %s\n", argv[2]);
    return 2;
}
